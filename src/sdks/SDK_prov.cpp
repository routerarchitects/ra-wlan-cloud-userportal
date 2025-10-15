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

	namespace Configuration {
		bool Get(RESTAPIHandler *client, const std::string &ConfigUUID,
				 ProvObjects::DeviceConfiguration &Config) {
			std::string EndPoint = "/api/v1/configuration/" + ConfigUUID;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Config.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool Delete(RESTAPIHandler *client, const std::string &ConfigUUID) {
			std::string EndPoint = "/api/v1/configuration/" + ConfigUUID;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto ResponseStatus =
				API.Do(client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				return true;
			}
			return false;
		}

		bool Create(RESTAPIHandler *client, const std::string &SerialNumber,
					const ProvObjects::DeviceConfiguration &Config, std::string &ConfigUUID) {
			std::string EndPoint = "/api/v1/configuration/0";
			Poco::JSON::Object Body;
			Config.to_json(Body);

			std::stringstream OOS;
			Body.stringify(OOS);

			auto API = OpenAPIRequestPost(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				std::ostringstream OS;
				CallResponse->stringify(OS);
				// std::cout << "CREATE: " << OS.str() << std::endl;
				return false;
			}

			ProvObjects::DeviceConfiguration NewConfig;
			NewConfig.from_json(CallResponse);
			ConfigUUID = NewConfig.info.id;

			Body.clear();
			Body.set("serialNumber", SerialNumber);
			Body.set("deviceConfiguration", ConfigUUID);
			EndPoint = "/api/v1/inventory/" + SerialNumber;
			auto API2 = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
			CallResponse->clear();
			ResponseStatus = API2.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				std::ostringstream OS;
				CallResponse->stringify(OS);
				return false;
			}
			return true;
		}

		bool Update(RESTAPIHandler *client, const std::string &ConfigUUID,
					ProvObjects::DeviceConfiguration &Config) {
			std::string EndPoint = "/api/v1/configuration/" + ConfigUUID;
			Poco::JSON::Object Body;
			Config.to_json(Body);
			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return true;
		}

		bool Push(RESTAPIHandler *client, const std::string &serialNumber,
				  ProvObjects::InventoryConfigApplyResult &Results) {
			std::string EndPoint = "/api/v1/inventory/" + serialNumber;
			Poco::JSON::Object Body;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint,
										 {{"applyConfiguration", "true"}}, 30000);

			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				Results.from_json(CallResponse);
				return true;
			}

			std::ostringstream OO;
			CallResponse->stringify(OO);
			return false;
		}
	} // namespace Configuration

	namespace Subscriber {
		bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &OperatorId, ProvObjects::SubscriberDeviceList &devList) {

			std::string EndPoint = "/api/v1/subscriberDevice";
			auto API = OpenAPIRequestGet(
				uSERVICE_PROVISIONING, EndPoint,
				{{"subscriberId", SubscriberId}, {"operatorId", OperatorId}}, 60000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return devList.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool UpdateSubscriber(RESTAPIHandler *client, const std::string &subscriberId,
							const std::string &serialNumber, bool removeSubscriber /* = false */) {
			const std::string endpoint = "/api/v1/inventory/" + serialNumber;

			Poco::JSON::Object body;
			body.set("devClass", "subscriber");
			body.set("subscriber", subscriberId);

			// action for logging purpose only
			const std::string action = removeSubscriber ? "remove" : "add";

			Poco::Logger::get("SDK_prov").debug(fmt::format(
				"Attempting to {} subscriber [{}] in inventory (serialNumber: {}).",
				action, subscriberId, serialNumber));
			Types::StringPairVec queryParams {};
			if (removeSubscriber) {
				queryParams.emplace_back("removeSubscriber", subscriberId);
			}

			auto api = OpenAPIRequestPut(uSERVICE_PROVISIONING, endpoint, queryParams, body, 20000);
			Poco::Logger::get("SDK_prov").information(fmt::format(
				"endpoint: [{}], queryParams: [{}]", endpoint,
				(removeSubscriber ? "removeSubscriber=" + subscriberId
								  : "")));

			auto response = Poco::makeShared<Poco::JSON::Object>();
			if (api.Do(response, client ? client->UserInfo_.webtoken.access_token_ : "") !=
				Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format(
					"Failed to {} subscriber [{}] in inventory (serialNumber: {}).",
					action, subscriberId, serialNumber));
				return false;
			}
			Poco::Logger::get("SDK_prov").debug(fmt::format(
				"Successfully {} subscriber [{}] in inventory (serialNumber: {}).",
				action, subscriberId, serialNumber));
			return true;
		}

		bool SetDevice(RESTAPIHandler *client, const ProvObjects::SubscriberDevice &D) {
			std::string EndPoint = "/api/v1/subscriberDevice/" + D.info.id;
			Poco::JSON::Object Body;
			D.to_json(Body);
			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, Body, 120000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return true;
		}

		bool GetDevice(RESTAPIHandler *client, const std::string &SerialNumber,
					   ProvObjects::SubscriberDevice &D) {
			std::string EndPoint = "/api/v1/subscriberDevice/" + SerialNumber;
			Poco::JSON::Object Body;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return D.from_json(CallResponse);
		}
	} // namespace Subscriber

} // namespace OpenWifi::SDK::Prov
