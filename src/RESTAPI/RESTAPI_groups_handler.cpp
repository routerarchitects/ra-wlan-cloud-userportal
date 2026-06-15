/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_groups_handler.h"
#include "sdks/SDK_parental_control.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_gw.h"
#include "framework/utils.h"
#include "Poco/JSON/Array.h"
#include "Poco/JSON/Stringifier.h"
#include "fmt/format.h"

namespace OpenWifi {

	namespace {
		bool IsValidConfigRawCommand(const Poco::JSON::Array::Ptr &command) {
			return command && command->size() >= 2 && command->get(0).isString() &&
				   command->get(1).isString();
		}

		bool ExtractDeleteConfigRaw(const Poco::JSON::Object::Ptr &callResponse,
									Poco::JSON::Array::Ptr &deleteConfigRaw) {
			deleteConfigRaw.reset();
			if (!callResponse) {
				return true;
			}
			if (!callResponse->has("config-raw")) {
				return true;
			}
			if (!callResponse->isArray("config-raw")) {
				return false;
			}
			deleteConfigRaw = callResponse->getArray("config-raw");
			if (!deleteConfigRaw) {
				return false;
			}
			for (std::size_t i = 0; i < deleteConfigRaw->size(); ++i) {
				if (!deleteConfigRaw->isArray(i)) {
					return false;
				}
				if (!IsValidConfigRawCommand(deleteConfigRaw->getArray(i))) {
					return false;
				}
			}
			return true;
		}
	} // namespace

	// Strips all existing config-raw entries whose UCI path (index 1) starts with
	// "parental_control" or "parental_control.", then appends the parental-control
	// service's own config-raw entries (from the DELETE response).
	// If the result is empty, removes config-raw entirely.
	static void MergeConfigRaw(Poco::JSON::Object::Ptr &gatewayConfig, const Poco::JSON::Array::Ptr &deleteConfigRaw) {
		auto mergedRaw = Poco::makeShared<Poco::JSON::Array>();

		if (gatewayConfig->has("config-raw") && gatewayConfig->isArray("config-raw")) {
			auto currentRaw = gatewayConfig->getArray("config-raw");
			for (std::size_t i = 0; i < currentRaw->size(); ++i) {
				if (!currentRaw->isArray(i))
					continue;
				auto cmd = currentRaw->getArray(i);
				if (!cmd || cmd->size() < 2)
					continue;
				if (cmd->get(1).isString()) {
					std::string path = cmd->getElement<std::string>(1);
					if (path == "parental_control" || path.find("parental_control.") == 0)
						continue;
				}
				mergedRaw->add(cmd);
			}
		}

		if (deleteConfigRaw) {
			for (std::size_t i = 0; i < deleteConfigRaw->size(); ++i) {
				if (!deleteConfigRaw->isArray(i))
					continue;
				auto cmd = deleteConfigRaw->getArray(i);
				if (cmd)
					mergedRaw->add(cmd);
			}
		}

		if (mergedRaw->size() > 0) {
			gatewayConfig->set("config-raw", mergedRaw);
		} else {
			gatewayConfig->remove("config-raw");
		}
	}

	void RESTAPI_groups_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (SDK::ParentalControl::GetGroup(this, UserInfo_.userinfo.id, groupId, callStatus,
		                                   callResponse)) {
			return ReturnObject(*callResponse);
		}
		return ForwardErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoPut() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		// GroupPutRequest: additionalProperties: false — reject unknown fields.
		std::vector<std::string> names;
		ParsedBody_->getNames(names);
		for (const auto &name : names) {
			if (name != "name" && name != "description") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
				                  "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("name") || ParsedBody_->isNull("name") ||
		    !ParsedBody_->get("name").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "name is required");
		}
		std::string nameVal = ParsedBody_->getValue<std::string>("name");
		Poco::trimInPlace(nameVal);
		if (nameVal.empty()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
			                  "name must be non-empty");
		}

		// description is required for PUT (full replacement); may be null.
		if (!ParsedBody_->has("description")) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
			                  "description is required for PUT");
		}

		Poco::JSON::Object body;
		body.set("name", nameVal);
		if (ParsedBody_->isNull("description")) {
			body.set("description", Poco::Dynamic::Var());
		} else {
			if (!ParsedBody_->get("description").isString()) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
				                  "description must be a string or null");
			}
			std::string descVal = ParsedBody_->getValue<std::string>("description");
			Poco::trimInPlace(descVal);
			body.set("description", descVal);
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (SDK::ParentalControl::UpdateGroup(this, UserInfo_.userinfo.id, groupId, body,
		                                      callStatus, callResponse)) {
			return ReturnObject(*callResponse);
		}
		return ForwardErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoDelete() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		std::string rawResponseBody;

		if (!SDK::ParentalControl::DeleteGroup(this, UserInfo_.userinfo.id, groupId, callStatus,
		                                       callResponse, rawResponseBody)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr deleteConfigRaw;
		if (!ExtractDeleteConfigRaw(callResponse, deleteConfigRaw)) {
			Logger().error(fmt::format(
			    "DoDelete: invalid parental-control delete payload "
			    "(subscriber={} group={})",
			    UserInfo_.userinfo.id, groupId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		// If the parental-control service returned a body with config-raw entries,
		// apply the merged config to the subscriber's gateway device.
		if (deleteConfigRaw && deleteConfigRaw->size() > 0) {
			const std::string operatorId = UserInfo_.userinfo.owner;
			if (operatorId.empty()) {
				Logger().error(fmt::format(
				    "DoDelete: operator id missing for gateway apply "
				    "(subscriber={} group={})",
				    UserInfo_.userinfo.id, groupId));
				return UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
			}

			ProvObjects::SubscriberDeviceList devList;
			Poco::Net::HTTPResponse::HTTPStatus provStatus;
			Poco::JSON::Object::Ptr provResponse;
			if (!SDK::Prov::Subscriber::GetDevices(this, UserInfo_.userinfo.id, operatorId,
			                                       devList, provStatus, provResponse)) {
				return ForwardErrorResponse(this, provStatus, provResponse);
			}

			std::string gatewaySerial;
			for (const auto &dev : devList.subscriberDevices) {
				std::string grp = dev.deviceGroup;
				Poco::toLowerInPlace(grp);
				if (grp == "olg") {
					gatewaySerial = dev.serialNumber;
					break;
				}
			}

			if (gatewaySerial.empty()) {
				Logger().error(fmt::format(
				    "DoDelete: gateway serial not resolved "
				    "(subscriber={} group={})",
				    UserInfo_.userinfo.id, groupId));
				return InternalError(RESTAPI::Errors::MissingSerialNumber);
			}

			Poco::JSON::Object::Ptr gwResponse;
			Poco::Net::HTTPResponse::HTTPStatus gwStatus;
			if (!SDK::GW::Device::GetConfig(this, gatewaySerial, gwStatus, gwResponse)) {
				return ForwardErrorResponse(this, gwStatus, gwResponse);
			}

			if (!gwResponse || !gwResponse->has("configuration") ||
			    !gwResponse->isObject("configuration")) {
				Logger().error(
				    fmt::format("DoDelete: gateway config malformed (serial={})", gatewaySerial));
				return InternalError(RESTAPI::Errors::InternalError);
			}

			auto gatewayConfig = gwResponse->getObject("configuration");
			MergeConfigRaw(gatewayConfig, deleteConfigRaw);

			Poco::JSON::Object::Ptr configureResponse;
			Poco::Net::HTTPResponse::HTTPStatus configureStatus;
			if (!SDK::GW::Device::Configure(this, gatewaySerial, gatewayConfig,
			                                configureStatus, configureResponse)) {
				return ForwardErrorResponse(this, configureStatus, configureResponse);
			}
		}

		// YAML contract: DELETE public success returns empty HTTP 200.
		return OK();
	}

} // namespace OpenWifi
