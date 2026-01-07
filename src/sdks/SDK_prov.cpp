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
#include "framework/ow_constants.h"
#include "nlohmann/json.hpp"

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
		/*
		   DeleteInventoryDevice:
		   1. Send a delete request to Provisioning to remove this device from the Inventory list (by serial number).
		   2. If Provisioning confirms the delete (HTTP 200 OK), return true.
		   3. Otherwise log an error and return false.
		*/
		bool DeleteInventoryDevice(RESTAPIHandler *client, const std::string &SerialNumber) {
			std::string EndPoint = "/api/v1/inventory/" + SerialNumber;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto ResponseStatus = API.Do(client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Failed to delete inventory device [{}]", SerialNumber));
			}
			return ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK;
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
		/*
			BuildMeshConfig():
			1. Take the fetched gateway configuration and parse it into JSON.
			2. For each interface(WAN/LAN), clone it and set ipv4.addressing to dynamic on the LAN.
			3. Convert that JSON back into a Poco object so callers can use the new config as needed.
		*/
		Poco::JSON::Object::Ptr BuildMeshConfig(const Poco::JSON::Object::Ptr &configuration) {
			auto gatewayConfig = configuration->getObject("configuration");
			std::ostringstream OS;
			Poco::JSON::Stringifier::stringify(gatewayConfig, OS);
			auto cfg = nlohmann::json::parse(OS.str());
			if (cfg.contains("interfaces") && cfg["interfaces"].is_array() && !cfg["interfaces"].empty()) {
				nlohmann::json meshInterfaces = nlohmann::json::array();
				for (const auto &iface : cfg["interfaces"]) {
					auto meshInterface = iface;
					meshInterface["ipv4"] = {{"addressing", "dynamic"}};
					meshInterfaces.push_back(meshInterface);
				}
				if (!meshInterfaces.empty()) {
					cfg["interfaces"] = meshInterfaces;
				}
			}
			Poco::JSON::Parser parser;
			auto parsed = parser.parse(cfg.dump());
			return parsed.extract<Poco::JSON::Object::Ptr>();
		}

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

		/*
			CreateSubDeviceInfo:
			1. Check if a subscriberDevice already exists in subdevice table for the serial and return it if found.
			2. If missing, build a minimal subscriberDevice from the inventory tag and user info:
			   - set name to serial, fill serial/deviceType/operator/subscriber/realMac/deviceRules
			   - populate object info via CreateObjectInfo
			3. POST it via CreateDevice() (/api/v1/subscriberDevice/0) so the service assigns the UUID and returns the persisted record.
		*/
		bool CreateSubDeviceInfo(RESTAPIHandler *client, const ProvObjects::InventoryTag &inventoryTag, const SecurityObjects::UserInfo &userInfo,
							  ProvObjects::SubscriberDevice &device) {
			if (GetDevice(client, inventoryTag.serialNumber, device)) {
				return true;
			}
			Poco::Logger::get("SDK_prov").information(fmt::format("Creating subscriberDevice data for {}", inventoryTag.serialNumber));

			device.info.name = inventoryTag.serialNumber;
			device.serialNumber = inventoryTag.serialNumber;
			device.deviceType = inventoryTag.deviceType;
			device.operatorId = userInfo.owner;
			device.subscriberId = userInfo.id;
			device.realMacAddress = inventoryTag.serialNumber;
			device.deviceRules = inventoryTag.deviceRules;
			ProvObjects::CreateObjectInfo(userInfo, device.info);

			return CreateDevice(client, device);
		}

		/*
			CreateDevice:
			1. POST the subscriberDevice to /api/v1/subscriberDevice/0 (provisioning assigns the UUID).
			2. On HTTP 200, overwrite the same object with the provisioning response (info.id, defaults).
		*/
		bool CreateDevice(RESTAPIHandler *client, ProvObjects::SubscriberDevice &device) {
			// Use "0" to let provisioning assign the UUID and return the created subdevice.
			std::string EndPoint = "/api/v1/subscriberDevice/0";
			Poco::JSON::Object Body;
			device.to_json(Body);
			auto API = OpenAPIRequestPost(uSERVICE_PROVISIONING, EndPoint, {}, Body, 120000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return device.from_json(CallResponse);
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
		/*
		   DeleteProvSubscriberDevice:
		   1. Find the device entry in Provisioning-subscriber_device database using the device serial number.
		   2. If no device exist return error.
		   3. If it is found, send a delete request to Provisioning to remove that device entry.
		   4. Return success if Provisioning confirms the delete (HTTP 200 OK).
		*/
		bool DeleteProvSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber) {
			ProvObjects::SubscriberDevice device;
			if (!GetDevice(client, SerialNumber, device) || device.info.id.empty()) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Could not find device [{}]", SerialNumber));
				return false;
			}
			std::string EndPoint = "/api/v1/subscriberDevice/" + device.info.id;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto ResponseStatus = API.Do(client ? client->UserInfo_.webtoken.access_token_ : "");
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Failed to delete device [{}] from provisioning subdevice table ", SerialNumber));
			}
			return ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK;
		}
	} // namespace Subscriber

	namespace Signup {
		bool UpdateSignupDevice(RESTAPIHandler *client, const std::string &userId, const std::string &macAddress) {
			Poco::JSON::Object body;

			std::string endpoint = "/api/v1/signup";
			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, endpoint, {{"userId", userId}, {"operation", "updateMac"}, {"mac", macAddress}}, body, 10000);
			auto Response = Poco::makeShared<Poco::JSON::Object>();
			const auto status = API.Do(Response, client ? client->UserInfo_.webtoken.access_token_ : "");

			if (status != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Failed to update signup mac for [{}]", userId));
				return false;
			}
			return true;
		}
	} // namespace Signup
} // namespace OpenWifi::SDK::Prov
