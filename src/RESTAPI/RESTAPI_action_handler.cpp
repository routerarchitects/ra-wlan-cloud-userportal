/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "framework/utils.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"
#include "Poco/String.h"

namespace OpenWifi {

	struct ActionContext {
		std::string Command;
		std::string Mac;
		std::string TargetSerial;
		std::string GatewaySerial;
		std::string ImageName;
		std::string Pattern{"blink"};
		uint64_t When = 0;
		uint64_t Duration = 30;
		bool KeepRedirector = true;
		ProvObjects::SubscriberDeviceList SubscriberDevices;
	};

	bool RESTAPI_action_handler::ParseRequest(ActionContext &ctx) {
		ctx.Command = GetParameter("action", "");
		Poco::trimInPlace(ctx.Command);
		Poco::toLowerInPlace(ctx.Command);
		if (ctx.Command.empty()) {
			BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
			return false;
		}

		AssignIfPresent(ParsedBody_, "mac", ctx.Mac);
		Poco::trimInPlace(ctx.Mac);
		Poco::toLowerInPlace(ctx.Mac);
		if (ctx.Command != "configure") {
			if (ctx.Mac.empty()) {
				BadRequest(RESTAPI::Errors::MissingSerialNumber);
				return false;
			}
			if (!Utils::NormalizeMac(ctx.Mac)) {
				BadRequest(RESTAPI::Errors::InvalidMacAddress);
				return false;
			}
		}

		AssignIfPresent(ParsedBody_, "when", ctx.When);
		AssignIfPresent(ParsedBody_, "duration", ctx.Duration);
		AssignIfPresent(ParsedBody_, "uri", ctx.ImageName);
		AssignIfPresent(ParsedBody_, "pattern", ctx.Pattern);
		AssignIfPresent(ParsedBody_, "keepRedirector", ctx.KeepRedirector);
		Poco::trimInPlace(ctx.ImageName);
		return true;
	}

	bool RESTAPI_action_handler::FetchSubscriberDevices(ActionContext &ctx) {
		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Prov::Subscriber::GetDevices(nullptr, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
											   ctx.SubscriberDevices, callStatus, callResponse)) {
			if (callStatus == Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND) {
				BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
				return false;
			}
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				ForwardErrorResponse(this, callStatus, callResponse);
				return false;
			}
			InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		if (ctx.SubscriberDevices.subscriberDevices.empty()) {
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		return true;
	}

	bool RESTAPI_action_handler::FindGatewaySerial(ActionContext &ctx) {
		for (const auto &device : ctx.SubscriberDevices.subscriberDevices) {
			auto group = device.deviceGroup;
			Poco::toLowerInPlace(group);
			if (group == "olg") {
				ctx.GatewaySerial = device.serialNumber;
				break;
			}
		}
		if (ctx.GatewaySerial.empty()) {
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		return true;
	}

	bool RESTAPI_action_handler::ResolveTargetSerial(ActionContext &ctx) {
		if (ctx.Command == "configure")
			return true;

		for (const auto &device : ctx.SubscriberDevices.subscriberDevices) {
			std::string serial = device.serialNumber;
			if (Utils::NormalizeMac(serial) && serial == ctx.Mac) {
				ctx.TargetSerial = device.serialNumber;
				break;
			}

			std::string realMac = device.realMacAddress;
			if (Utils::NormalizeMac(realMac) && realMac == ctx.Mac) {
				ctx.TargetSerial = device.serialNumber;
				break;
			}
		}

		if (ctx.TargetSerial.empty()) {
			NotFound();
			return false;
		}
		return true;
	}

	void RESTAPI_action_handler::ReturnSDKResponse(
		Poco::Net::HTTPResponse::HTTPStatus status, const Poco::JSON::Object::Ptr &response) {
		auto payload = response ? response : Poco::makeShared<Poco::JSON::Object>();
		if (status != Poco::Net::HTTPResponse::HTTP_OK) {
			return ForwardErrorResponse(this, status, payload);
		}
		if (payload->size() != 0) {
			return ReturnObject(*payload);
		}
		return OK();
	}

	bool RESTAPI_action_handler::ExecuteConfigure(ActionContext &ctx) {
		Poco::Net::HTTPResponse::HTTPStatus callStatus =
			Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr callResponse;

		SDK::GW::Device::SetConfig(nullptr, ParsedBody_, ctx.SubscriberDevices, ctx.GatewaySerial,
								   callStatus, callResponse);
		ReturnSDKResponse(callStatus, callResponse);
		return callStatus == Poco::Net::HTTPResponse::HTTP_OK;
	}

	bool RESTAPI_action_handler::ExecuteCommand(ActionContext &ctx) {
		Poco::Net::HTTPResponse::HTTPStatus callStatus =
			Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr callResponse;
		bool commandSuccess = false;

		if (ctx.Command == "reboot") {
			commandSuccess =
				SDK::GW::Device::Reboot(nullptr, ctx.TargetSerial, ctx.When, callStatus, callResponse);
		} else if (ctx.Command == "blink") {
			commandSuccess = SDK::GW::Device::LEDs(nullptr, ctx.TargetSerial, ctx.When, ctx.Duration,
												   ctx.Pattern, callStatus, callResponse);
		} else if (ctx.Command == "upgrade") {
			commandSuccess = SDK::GW::Device::Upgrade(nullptr, ctx.TargetSerial, ctx.When,
													  ctx.ImageName, ctx.KeepRedirector, callStatus,
													  callResponse);
		} else if (ctx.Command == "factory") {
			commandSuccess = SDK::GW::Device::Factory(nullptr, ctx.TargetSerial, ctx.When,
													  ctx.KeepRedirector, callStatus, callResponse);
		} else {
			BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
			return false;
		}

		ReturnSDKResponse(callStatus, callResponse);
		return commandSuccess;
	}

	void RESTAPI_action_handler::DoPost() {
		if (!ParsedBody_) {
			ParsedBody_ = Poco::makeShared<Poco::JSON::Object>();
		}

		ActionContext ctx;
		if (!ParseRequest(ctx))
			return;
		if (!FetchSubscriberDevices(ctx))
			return;
		if (!FindGatewaySerial(ctx))
			return;
		if (!ResolveTargetSerial(ctx))
			return;

		if (ctx.Command == "configure") {
			ExecuteConfigure(ctx);
			return;
		}
		ExecuteCommand(ctx);
	}
} // namespace OpenWifi
