/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#include "SDK_prov.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/RESTAPI_utils.h"

namespace OpenWifi::SDK::Prov {

	namespace Device {
		bool Get(RESTAPIHandler *client, const std::string &Mac,
				 ProvObjects::InventoryTag &Device) {
			std::string EndPoint = "/api/v1/inventory/" + Mac;

			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();

			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Device.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}
	} // namespace Device

	namespace Venue {
		bool Get(RESTAPIHandler *client, const std::string &VenueUUID, ProvObjects::Venue &Venue,
				 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
				 Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue/" + VenueUUID;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Venue.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}
	} // namespace Venue

	namespace Subscriber {
		bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId, const std::string &OperatorId,
						ProvObjects::SubscriberDeviceList &devList,
						Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {

			std::string EndPoint = "/api/v1/subscriberDevice";
			auto API = OpenAPIRequestGet(
				uSERVICE_PROVISIONING, EndPoint,
				{{"subscriberId", SubscriberId}, {"operatorId", OperatorId}}, 60000);
			CallStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return devList.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool CreateSubsciberDeviceMinimal(
			RESTAPIHandler *client, const std::string &name, const std::string &serialNumber,
			const std::string &subscriberId, const std::string &deviceGroup,
			const ProvObjects::DeviceConfigurationElementVec &configuration,
			ProvObjects::SubscriberDevice &device) {
			std::string EndPoint = "/api/v1/subscriberDevice/0";
			Poco::JSON::Object Body;
			Body.set("name", name);
			Body.set("serialNumber", serialNumber);
			Body.set("subscriberId", subscriberId);
			Body.set("deviceGroup", deviceGroup);
			RESTAPI_utils::field_to_json(Body, "configuration", configuration);
			auto API = OpenAPIRequestPost(uSERVICE_PROVISIONING, EndPoint, {}, Body, 120000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus =
				API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return device.from_json(CallResponse);
		}

		bool DeleteSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber,
									Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus) {
			std::string EndPoint = "/api/v1/subscriberDevice/" + SerialNumber;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallStatus = API.Do(client ? client->UserInfo_.webtoken.access_token_ : "");
			if (CallStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Failed to delete device [{}] from provisioning subdevice table ", SerialNumber));
			}
			return CallStatus == Poco::Net::HTTPResponse::HTTP_OK;
		}
	} // namespace Subscriber
} // namespace OpenWifi::SDK::Prov
