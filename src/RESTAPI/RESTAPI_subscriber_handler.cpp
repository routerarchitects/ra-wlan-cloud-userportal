/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-11-07.
//
#include "framework/ow_constants.h"

#include "RESTAPI_subscriber_handler.h"
#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "SubscriberCache.h"
#include "framework/utils.h"
#include "nlohmann/json.hpp"
#include "sdks/SDK_fms.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_sec.h"

#include <optional>

namespace OpenWifi {

	bool RESTAPI_subscriber_handler::ValidateUserInfo() {
		if (UserInfo_.userinfo.id.empty()) {
			NotFound();
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_handler::LoadSubscriberInfo(SubObjects::SubscriberInfo &subInfo) {
		if (StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, subInfo)) {
			Poco::JSON::Object Answer;
			subInfo.to_json(Answer);
			Logger().debug(
				fmt::format("{}: Returning existing subscriber information.", UserInfo_.userinfo.email));
			ReturnObject(Answer);
			return true;
		}
		return false;
	}

	static void SetErrorResponse(RESTAPIHandler *client, Poco::Net::HTTPResponse::HTTPStatus callStatus,
								const Poco::JSON::Object::Ptr &callResponse) {
		auto Forward = callResponse ? callResponse : Poco::makeShared<Poco::JSON::Object>();
		if (client && client->Request && Forward->has("ErrorDetails")) {
			Forward->set("ErrorDetails", client->Request->getMethod());
		}
		client->Response->setStatus(callStatus);
		std::stringstream ss;
		Poco::JSON::Stringifier::condense(Forward, ss);
		client->Response->setContentType("application/json");
		client->Response->setContentLength(ss.str().size());
		auto &os = client->Response->send();
		os << ss.str();
	}

	bool RESTAPI_subscriber_handler::LoadProvisioningDevices(ProvObjects::SubscriberDeviceList &devices) {

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr callResponse = Poco::makeShared<Poco::JSON::Object>();

		if (!SDK::Prov::Subscriber::GetDevices(this, UserInfo_.userinfo.id,
												UserInfo_.userinfo.owner, devices, callStatus, callResponse)) {
			SetErrorResponse(this, callStatus, callResponse);
			return false;
		}

		if (devices.subscriberDevices.empty()) {
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_handler::PrepareSubInfoObject(
		SubObjects::SubscriberInfo &subInfo, const ProvObjects::SubscriberDeviceList &devices) {
		StorageService()->SubInfoDB().BuildDefaultSubscriberInfo(UserInfo_, subInfo, devices);
		return true;
	}

	bool RESTAPI_subscriber_handler::PrepareDefaultConfig(
		SubObjects::SubscriberInfo &subInfo, ProvObjects::SubscriberDevice &subDevice) {
		Logger().debug(
			fmt::format("{}: Fetching Current configuration from controller", UserInfo_.userinfo.email));
		if (subInfo.accessPoints.list.empty()) {
			Logger().error("Subscriber access points list is empty.");
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}

		ConfigMaker InitialConfig(Logger(), subInfo.id);
		const std::string targetMac = subInfo.accessPoints.list[0].macAddress;

		if (!SDK::Prov::Subscriber::GetDevice(this, targetMac, subDevice)) {
			Logger().error(fmt::format("Could not find provisioning subdevice for {}.", targetMac));
			InternalError(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		if (!InitialConfig.PrepareDefaultConfig(subInfo.accessPoints.list[0], subDevice)) {
			Logger().error(fmt::format("Failed to create PrepareDefaultConfig: Fetched config is invalid for MAC {}.",
									   subInfo.accessPoints.list[0].macAddress));
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_handler::LinkSubscriberDevice(
		const SubObjects::SubscriberInfo &subInfo, const ProvObjects::SubscriberDevice &subDevice) {
		if (!SDK::Prov::Subscriber::SetDevice(this, subDevice)) {
			Logger().error(
				fmt::format("Failed to persist provisioning config for {}.", subDevice.serialNumber));
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}
		if (!SDK::GW::Device::SetSubscriber(this, subDevice.serialNumber, subInfo.id)) {
			Logger().error(fmt::format("Failed to link device {} to subscriber {} in gateway.",
									   subDevice.serialNumber, subInfo.id));
		}
		if (!SDK::Prov::Subscriber::SetSubscriber(this, subInfo.id, subDevice.serialNumber, false)) {
			Logger().error(fmt::format("Couldn't link device {} to subscriber {} in inventory.",
									   subDevice.serialNumber, subInfo.id));
		}
		return true;
	}

	bool RESTAPI_subscriber_handler::CreateDbEntry(
		SubObjects::SubscriberInfo &subInfo) {
		Logger().debug(
			fmt::format("{}: Creating default user information.", UserInfo_.userinfo.email));
		StorageService()->SubInfoDB().CreateRecord(subInfo);
		return true;
	}

	bool RESTAPI_subscriber_handler::ProvisionSubscriber(SubObjects::SubscriberInfo &subInfo) {
		Poco::Net::HTTPServerResponse::HTTPStatus provisionStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto provisionResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Prov::Subscriber::ProvisionSubscriber(
				this, UserInfo_.userinfo.id, true, std::nullopt, std::nullopt, std::nullopt,
				provisionStatus, provisionResponse)) {
			SetErrorResponse(this, provisionStatus, provisionResponse);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_handler::DeletePostSubscriber() {
		Poco::Net::HTTPServerResponse::HTTPStatus provisionStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		if (!SDK::Prov::Subscriber::DeleteProvisionSubscriber(
				this, UserInfo_.userinfo.id, provisionStatus)) {
			SetErrorResponse(this, provisionStatus, Poco::makeShared<Poco::JSON::Object>());
			return false;
		}
		return true;
	}

/*
	GET /api/v1/subscriber on a first call:
	1. Fetch provisioning subdevice record + device current config from controller.
	2. Build default SSID/password and provisioning config from the controller data.
	3. Persist the provisioning subdevice record, link the device to the subscriber (gateway + inventory), then store the subscriber record in the database.
*/
	void RESTAPI_subscriber_handler::DoGet() {
		struct InitSubscriberContext {
			SubObjects::SubscriberInfo SubscriberInfo{};
			ProvObjects::SubscriberDeviceList Devices{};
			ProvObjects::SubscriberDevice SubDevice{};
		} ctx;

		if (!ValidateUserInfo()) return;
		Logger().debug(fmt::format("{}: Get Subscriber", UserInfo_.userinfo.email));

		if (LoadSubscriberInfo(ctx.SubscriberInfo)) return;
		if (!LoadProvisioningDevices(ctx.Devices)) return;
		if (!PrepareSubInfoObject(ctx.SubscriberInfo, ctx.Devices)) return;
		if (!PrepareDefaultConfig(ctx.SubscriberInfo, ctx.SubDevice)) return;
		if (!LinkSubscriberDevice(ctx.SubscriberInfo, ctx.SubDevice)) return;
		if (!CreateDbEntry(ctx.SubscriberInfo)) return;
		if (!ProvisionSubscriber(ctx.SubscriberInfo)) return;
		Poco::JSON::Object Answer;
		ctx.SubscriberInfo.to_json(Answer);
		ReturnObject(Answer);
	}

	void RESTAPI_subscriber_handler::DoDelete() {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().warning("Received delete request without id.");
			return NotFound();
		}

		Logger().information(fmt::format("Deleting subscriber {}.", UserInfo_.userinfo.id));

		if (!DeletePostSubscriber()) {
			return;
		}

		SubObjects::SubscriberInfo SI;
		if (StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI)) {
			for (const auto &i : SI.accessPoints.list) {
				if (!i.serialNumber.empty()) {
					SDK::Prov::Subscriber::SetSubscriber(nullptr, UserInfo_.userinfo.id,
																   i.serialNumber, true);
					SDK::GW::Device::SetSubscriber(nullptr, i.serialNumber, "");
				}
			}
			Logger().information(fmt::format("Deleting security info for {}.",
											 UserInfo_.userinfo.id));
			SDK::Sec::Subscriber::Delete(nullptr, UserInfo_.userinfo.id);
			Logger().information(fmt::format("Deleting subscriber info for {}.",
											 UserInfo_.userinfo.id));
			StorageService()->SubInfoDB().DeleteRecord("id", UserInfo_.userinfo.id);
			return OK();
		}
		return NotFound();
	}
} // namespace OpenWifi
