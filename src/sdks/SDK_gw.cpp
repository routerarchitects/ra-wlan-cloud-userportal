/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//
#include <regex>
#include "framework/RESTAPI_Handler.h"

#include "SDK_gw.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/utils.h"

namespace OpenWifi::SDK::GW {
	namespace Device {
		void Reboot(RESTAPIHandler *client, const std::string &Mac,
					[[maybe_unused]] uint64_t When) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/reboot";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", 0);
			PerformCommand(client, "reboot", EndPoint, ObjRequest);
		}

		void LEDs(RESTAPIHandler *client, const std::string &Mac, uint64_t When, uint64_t Duration,
				  const std::string &Pattern) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/leds";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("duration", Duration);
			ObjRequest.set("pattern", Pattern);
			PerformCommand(client, "leds", EndPoint, ObjRequest);
		}

		void Factory(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 bool KeepRedirector) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/factory";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("keepRedirector", KeepRedirector);
			PerformCommand(client, "factory", EndPoint, ObjRequest);
		}

		void Upgrade(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 const std::string &ImageName, [[maybe_unused]] bool KeepRedirector) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/upgrade";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("uri", ImageName);
			PerformCommand(client, "upgrade", EndPoint, ObjRequest);
		}

		void PerformCommand(RESTAPIHandler *client, const std::string &Command,
							const std::string &EndPoint, Poco::JSON::Object &CommandRequest) {
			auto API = OpenAPIRequestPost(uSERVICE_GATEWAY, EndPoint, {}, CommandRequest, 60000);
			Poco::JSON::Object::Ptr CallResponse;

			auto ResponseStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (client == nullptr) {
				return;
			}
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT) {
				Poco::JSON::Object ResponseObject;
				ResponseObject.set("Code", Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT);
				ResponseObject.set(
					"Details",
					"Command could not complete, you may want to retry this operation later.");
				ResponseObject.set("Operation", Command);
				client->Response->setStatus(ResponseStatus);
				std::stringstream SS;
				Poco::JSON::Stringifier::condense(ResponseObject, SS);
				client->Response->setContentLength(SS.str().size());
				client->Response->setContentType("application/json");
				auto &os = client->Response->send();
				os << SS.str();
			} else {
				client->Response->setStatus(ResponseStatus);
				std::stringstream SS;
				Poco::JSON::Stringifier::condense(CallResponse, SS);
				Poco::JSON::Parser P;
				auto Raw = P.parse(SS.str()).extract<Poco::JSON::Object::Ptr>();
				if (Raw->has("command") && Raw->has("errorCode") && Raw->has("errorText")) {
					Poco::JSON::Object ReturnResponse;
					ReturnResponse.set("Operation", Raw->get("command").toString());
					ReturnResponse.set("Details", Raw->get("errorText").toString());
					ReturnResponse.set("Code", Raw->get("errorCode"));

					std::stringstream Ret;
					Poco::JSON::Stringifier::condense(ReturnResponse, Ret);
					client->Response->setContentLength(Ret.str().size());
					client->Response->setContentType("application/json");
					auto &os = client->Response->send();
					os << Ret.str();
				}
			}
		}

		bool SetVenue(RESTAPIHandler *client, const std::string &SerialNumber,
					  const std::string &uuid) {
			Poco::JSON::Object Body;

			Body.set("serialNumber", SerialNumber);
			Body.set("venue", uuid);
			OpenWifi::OpenAPIRequestPut R(OpenWifi::uSERVICE_GATEWAY,
										  "/api/v1/device/" + SerialNumber, {}, Body, 10000);
			Poco::JSON::Object::Ptr Response;
			auto ResponseStatus =
				R.Do(Response, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				return true;
			}
			return false;
		}

		bool GetLastStats(RESTAPIHandler *client, const std::string &Mac,
						  Poco::JSON::Object::Ptr &Response) {
			// "https://${OWGW}/api/v1/device/$1/statistics?lastOnly=true"
			std::string EndPoint = "/api/v1/device/" + Mac + "/statistics";
			auto API = OpenAPIRequestGet(uSERVICE_GATEWAY, EndPoint, {{"lastOnly", "true"}}, 60000);
			auto ResponseStatus =
				API.Do(Response, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return true;
				} catch (...) {
					return false;
				}
			}
			return false;
		}

	    Poco::Net::HTTPResponse::HTTPStatus GetConfig(RESTAPIHandler *client, const std::string &Mac,
                                           Poco::JSON::Object::Ptr &Response) {
            std::string EndPoint = "/api/v1/device/" + Mac;
            auto API = OpenAPIRequestGet(uSERVICE_GATEWAY, EndPoint, {}, 1000);
            auto ResponseStatus =
                API.Do(Response, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if (ResponseStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
                Poco::Logger::get("SDK_gw").error(fmt::format(
                    "GetConfig: Could not get configuration from controller for device id {}. "
                    "Status={}",
                    Mac, int(ResponseStatus)));
                return ResponseStatus;
            }
            if (!Response || !Response->has("configuration")) {
                Poco::Logger::get("SDK_gw").error(fmt::format(
                    "GetConfig: Could not get configuration from controller for device id {}.", Mac));
                return ResponseStatus;
            }
			return Poco::Net::HTTPServerResponse::HTTP_OK;
        }
		/*
			Example of valid configuration format:
			{
				"interfaces": [
					{
						"ethernet": [
							{
								"select-ports": [
									"WAN*"
								]
							}
						],
						"ipv4": {
							"addressing": "dynamic"
						},
						"name": "WAN",
						"role": "upstream",
						"ssids": []
					},
					{
						"ethernet": [
							{
								"select-ports": [
									"LAN*"
								]
							}
						],
						"ipv4": {
							"addressing": "static",
							"dhcp": {
								"lease-count": 128,
								"lease-first": 1,
								"lease-time": "6h"
							},
							"gateway": "192.168.1.1",
							"subnet": "192.168.1.1/24"
						},
						"name": "LAN",
						"role": "downstream",
						"services": [
							"ssh",
							"lldp"
						],
						"ssids": [
							{
								"bss-mode": "ap",
								"encryption": {
									"ieee80211w": "required",
									"key": "Password-SSID",
									"proto": "psk2"
								},
								"hidden-ssid": false,
								"isolate-clients": false,
								"maximum-clients": 64,
								"name": "OpenWiFi-SSID",
								"roaming": true,
								"wifi-bands": [
									"2G",
									"5G"
								]
							},
							{
								"bss-mode": "mesh",
								"encryption": {
									"ieee80211w": "required",
									"key": "openwifi",
									"proto": "psk2"
								},
								"hidden-ssid": true,
								"isolate-clients": false,
								"maximum-clients": 64,
								"name": "Backhaul-SSID",
								"wifi-bands": [
									"5G"
								]
							}
						],
						"tunnel": {
							"proto": "mesh"
						}
					}
				]
			}
		*/

		/*
			ValidateConfig:
			1. Take the full device config, ensure the "configuration" field exists.
			2. Check interfaces are present, upstream ports have no SSIDs and ensure downstream interface contains static IpV4 addressing.
			3. Require at least one SSID with bss-mode == "mesh".
		*/
		bool ValidateConfig(const Poco::JSON::Object::Ptr &deviceConfig, const std::string &serialNumber, Poco::Logger &logger) {
			if (!deviceConfig || !deviceConfig->has("configuration")) {
				logger.error(fmt::format("Invalid configuration for device {}: missing configuration block.", serialNumber));
				return false;
			}
			auto configuration = deviceConfig->getObject("configuration");
			if (!configuration) {
				logger.error(fmt::format("Invalid configuration for device {}: Empty configuration.", serialNumber));
				return false;
			}
			auto interfaces = configuration->getArray("interfaces");
			if (!interfaces || interfaces->size() == 0) {
				logger.error(fmt::format("Invalid configuration for device {}: missing interfaces.", serialNumber));
				return false;
			}
			bool meshSsidFound = false;
			bool apSsidFound = false;
			for (std::size_t i = 0; i < interfaces->size(); ++i) {
				auto iface = interfaces->getObject(i);
				auto ssids = iface->getArray("ssids");
				std::string role = iface->getValue<std::string>("role");
				if (role == "upstream" && ssids && ssids->size() > 0) {
					logger.error(fmt::format("Invalid configuration for device {}: upstream interface contains SSIDs.", serialNumber));
					return false;
				}
				if (role == "downstream") {
					auto ipv4 = iface->getObject("ipv4");
					if (!ipv4 || ipv4->getValue<std::string>("addressing") != "static") {
						logger.error(fmt::format("Invalid configuration for device {}: downstream interface should have static IPv4 addressing.", serialNumber));
						return false;
					}
					auto tunnel = iface->getObject("tunnel");
					if (!tunnel || tunnel->getValue<std::string>("proto") != "mesh") {
						logger.error(fmt::format("Invalid configuration for device {}: downstream interface must have tunnel-proto='mesh'.", serialNumber));
						return false;
					}
				}
				if (!ssids)
					continue;
				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (ssid && ssid->getValue<std::string>("bss-mode") == "mesh")
						meshSsidFound = true;
					if (ssid && ssid->getValue<std::string>("bss-mode") == "ap")
						apSsidFound = true;
				}
			}
			if (!meshSsidFound) {
				logger.error(fmt::format("Invalid configuration for device {}: missing mesh SSID.", serialNumber));
				return false;
			}
			if (!apSsidFound) {
				logger.error(fmt::format("Invalid configuration for device {}: missing ap-SSID on downstream interface.", serialNumber));
				return false;
			}
			return true;
		}

		bool Configure(RESTAPIHandler *client, const std::string &Mac,
					   Poco::JSON::Object::Ptr &Configuration, Poco::JSON::Object::Ptr &Response) {

			Poco::JSON::Object Body;

			Poco::JSON::Parser P;
			uint64_t Now = Utils::Now();

			Configuration->set("uuid", Now);
			Body.set("serialNumber", Mac);
			Body.set("UUID", Now);
			Body.set("when", 0);
			Body.set("configuration", Configuration);

			OpenWifi::OpenAPIRequestPost R(OpenWifi::uSERVICE_GATEWAY,
										   "/api/v1/device/" + Mac + "/configure", {}, Body, 90000);

			auto ResponseStatus =
				R.Do(Response, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				std::ostringstream os;
				Poco::JSON::Stringifier::stringify(Response, os);
				return true;
			}

			return false;
		}

		/*
			Example of default configuration format:
				{
				"interfaces": [
					{
					"ethernet": [
						{
						"select-ports": [
							"WAN*"
						]
						}
					],
					"ipv4": {
						"addressing": "dynamic"
					},
					"name": "WAN",
					"role": "upstream",
					"services": [
						"lldp"
					]
					},
					{
					"ethernet": [
						{
						"select-ports": [
							"LAN*"
						]
						}
					],
					"ipv4": {
						"addressing": "static",
						"dhcp": {
						"lease-count": 100,
						"lease-first": 10,
						"lease-time": "24h"
						},
						"subnet": "192.168.1.1/24"
					},
					"name": "LAN",
					"role": "downstream",
					"services": [
						"lldp",
						"ssh"
					],
					"ssids": [
						{
						"bss-mode": "ap",
						"encryption": {
							"ieee80211w": "disabled",
							"key": "DEFAULT-PASSWORD",
							"proto": "psk2"
						},
						"name": "DEFAULT-SSID",
						"wifi-bands": [
							"2G",
							"5G"
						]
						}
					]
					}
				]
				}
        */
		// Validate SSID & password using helper functions validateSsid() & validatePassword()
		static bool validateSsid(std::string &ssid, std::string &status) {
			Poco::trimInPlace(ssid); // trim spaces around the SSID
			static const std::regex Ssid_Regex(R"(^[A-Za-z0-9._ \-]{1,32}$)");
			if (!std::regex_match(ssid, Ssid_Regex)) {
				status = "SSIDInvalidName";
				Poco::Logger::get("Configure").error(fmt::format("Invalid SSID ({}). Allowed: 1 to 32 chars (letters, numbers, dot, underscore, hyphen, space).", ssid));
				return false;
			}
			return true;
		}
		static bool validatePassword(const std::string &password, std::string &status) {
			static const std::regex Pass_Regex(R"(^\S{8,32}$)");
			if (!std::regex_match(password, Pass_Regex)) {
				status = "SSIDInvalidPassword";
				Poco::Logger::get("Configure").error("Invalid password. Must be 8 to 32 characters without spaces.");
				return false;
			}
			return true;
		}
		static bool SetInterfacesSsids(Poco::JSON::Object::Ptr &Config, const std::string &SerialNumber,
									 const Poco::JSON::Object::Ptr &Body, std::string &status) {
			status.clear(); //clear any previous status message
			std::string override_ssid{};
			std::string override_password{};

			// require at least one parameter
			if (!Body->has("ssid") && !Body->has("password")) {
				status = "MissingOrInvalidParameters";
				Poco::Logger::get("Configure").error("Missing Required Parameters.");
				return false;
			}
			// SSID validation first
			if (Body->has("ssid")) {
				std::string temp_ssid{};
				OpenWifi::RESTAPIHandler::AssignIfPresent(Body, "ssid", temp_ssid);
				if (!validateSsid(temp_ssid, status)) {
					return false;
				}
				override_ssid = temp_ssid;
			}
			// Password validation next
			if (Body->has("password")) {
				std::string temp_password{};
				OpenWifi::RESTAPIHandler::AssignIfPresent(Body, "password", temp_password);
				if (!validatePassword(temp_password, status)) {
					return false;
				}
				override_password = temp_password;
			}
			if (Config) {
				Poco::Logger::get("Configure").information(
					fmt::format("Applying SSID/Password overrides to device {}.", SerialNumber));
				try {
					auto interfaces = Config->getArray("interfaces");
					if (!interfaces || interfaces->size() == 0) {
						Poco::Logger::get("Configure").error(fmt::format("Missing or empty 'interfaces' for device {}.", SerialNumber));
						return false;
					}
					bool iface_ssids = false; // used a flag to ensure at least one SSID is found
					for (std::size_t i = 0; i < interfaces->size(); ++i) {
						auto iface = interfaces->getObject(i);
						auto ssids = iface->getArray("ssids");
						if (!ssids || ssids->size() == 0) // skip the interface being checked if no SSIDs is found
							continue; // eg: if WAN has no ssid but LAN has some
						iface_ssids = true;
						for (std::size_t j = 0; j < ssids->size(); ++j) {
							auto ssid = ssids->getObject(j);
							auto encrypt = ssid->getObject("encryption");
							if (!encrypt || encrypt->size() == 0) {
								Poco::Logger::get("Configure").error(fmt::format("Missing or invalid 'encryption' for device {}.", SerialNumber));
								return false;
							}
							if (!override_ssid.empty()) {
								ssid->set("name", override_ssid);
							Poco::Logger::get("Configure").information(
								fmt::format("Applied SSID override for device {}.", SerialNumber));
							}
							if (!override_password.empty()) {
								encrypt->set("key", override_password);
								Poco::Logger::get("Configure").information(fmt::format(
									"Applied password override for device {}.", SerialNumber));
							}
						}
					}
					if (!iface_ssids) {
						Poco::Logger::get("Configure").error(fmt::format("No SSID configuration found for device {}.", SerialNumber));
						return false;
					}
				} catch (const std::exception &ex) {
					Poco::Logger::get("Configure").error(fmt::format("SetInterfaceSsid: exception for device {}: {}", SerialNumber, ex.what()));
					status = "InternalError";
					return false;
				}
			}
			return true;
		}

		Poco::Net::HTTPResponse::HTTPStatus SetConfig(RESTAPIHandler *client, const std::string &SerialNumber,
					   const Poco::JSON::Object::Ptr &Body, std::string &status) {
			status.clear();
			Poco::JSON::Object::Ptr DeviceObj;
			auto returnStatus = GetConfig(client, SerialNumber, DeviceObj);
			if (returnStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				status = (returnStatus == Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND) ? "DeviceNotConnected": "InternalError";
				return returnStatus;
			}
			if (!DeviceObj || !DeviceObj->has("configuration")) {
				return Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
			}
			Poco::Logger::get("Configure").information( fmt::format("SetConfig: Fetched device config for serial {}.",SerialNumber));
			auto Config = DeviceObj->getObject("configuration");
			try { // Use SetInterfacesSsids to apply overrides
				if (!SetInterfacesSsids(Config, SerialNumber, Body, status)) {
					Poco::Logger::get("Configure").error("SetConfig: Failed to apply overrides");
					if (status.empty()){
						status = "InternalError";}
					return Poco::Net::HTTPResponse::HTTP_BAD_REQUEST;
				}
			} catch (const std::exception &ex) {
				// Catch standard exceptions (e.g., std::runtime_error) and log the error message
				Poco::Logger::get("Configure").error(
					fmt::format("SetConfig: exception while applying overrides for {}: {}",SerialNumber, ex.what()));
				status = "InternalError";
					return Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
			}
			Poco::JSON::Object::Ptr Response;
			if (Configure(client, SerialNumber, Config, Response)){ // Send new config to device
				Poco::Logger::get("Configure").information(fmt::format("SetConfig: Successfully sent new configuration to device {}.", SerialNumber));
				status.clear(); //clear status on success
				return Poco::Net::HTTPResponse::HTTP_OK;
			}
			status = "InternalError"; // Could not send config to device
			return Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
		}

		bool SetSubscriber(RESTAPIHandler *client, const std::string &SerialNumber,
						   const std::string &uuid) {
			Poco::JSON::Object Body;

			Body.set("serialNumber", SerialNumber);
			Body.set("subscriber", uuid);
			OpenWifi::OpenAPIRequestPut R(OpenWifi::uSERVICE_GATEWAY,
										  "/api/v1/device/" + SerialNumber, {}, Body, 10000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();
			auto ResponseStatus =
				R.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_gw").information(fmt::format(
					"SetSubscriber: Successfully set subscriber [{}] for device {}.", SerialNumber, uuid));
				return true;
			}
			return false;
		}

		struct Tag {
			std::string tag, value;
			bool from_json(const Poco::JSON::Object::Ptr &Obj) {
				try {
					OpenWifi::RESTAPI_utils::field_from_json(Obj, "tag", tag);
					OpenWifi::RESTAPI_utils::field_from_json(Obj, "value", value);
					return true;
				} catch (...) {
				}
				return false;
			}
		};

		struct TagList {
			std::vector<Tag> tagList;
			bool from_json(const Poco::JSON::Object::Ptr &Obj) {
				try {
					OpenWifi::RESTAPI_utils::field_from_json(Obj, "tagList", tagList);
					return true;
				} catch (...) {
				}
				return false;
			}
		};

		bool GetOUIs(RESTAPIHandler *client, Types::StringPairVec &MacListPair) {
			std::string EndPoint = "/api/v1/ouis";

			std::string MacList;
			for (const auto &i : MacListPair) {
				if (MacList.empty())
					MacList = i.first;
				else
					MacList += "," + i.first;
			}

			auto API = OpenAPIRequestGet(uSERVICE_GATEWAY, EndPoint, {{"macList", MacList}}, 60000);
			Poco::JSON::Object::Ptr Response;
			auto ResponseStatus =
				API.Do(Response, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					TagList TL;
					TL.from_json(Response);
					for (const auto &i : TL.tagList) {
						for (auto &j : MacListPair)
							if (j.first == i.tag)
								j.second = i.value;
					}
					return true;
				} catch (...) {
					return false;
				}
			}
			return false;
		}
		/*
		   DeleteOwgwDevice:
		   1. Issue a DELETE request to the gateway/controller for the given SerialNumber.
		   2. Return success only when owgw responds with HTTP 200 OK.
		*/
		bool DeleteOwgwDevice(RESTAPIHandler *client, const std::string &SerialNumber) {
			auto API = OpenAPIRequestDelete(uSERVICE_GATEWAY, "/api/v1/device/" + SerialNumber, {}, 15000);
			const auto status =	API.Do(client ? client->UserInfo_.webtoken.access_token_ : "");
			if (status != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_gw").error(fmt::format("Controller delete device [{}] failed.", SerialNumber));
				return false;
			}
			return true;
		}
	} // namespace Device
} // namespace OpenWifi::SDK::GW
