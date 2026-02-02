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
#include "sdks/SDK_prov.h"
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
				}
				if (!ssids)
					continue;
				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (ssid && ssid->getValue<std::string>("bss-mode") == "mesh")
						meshSsidFound = true;
				}
			}
			if (!meshSsidFound) {
				logger.error(fmt::format("Invalid configuration for device {}: missing mesh SSID.", serialNumber));
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
		static bool validateSsid(RESTAPIHandler *client, std::string &ssid) {
			Poco::trimInPlace(ssid); // trim spaces around the SSID
			static const std::regex Ssid_Regex(R"(^[A-Za-z0-9._ \-]{1,32}$)");
			if (!std::regex_match(ssid, Ssid_Regex)) {
				Poco::Logger::get("Configure").error(fmt::format("Invalid SSID ({}). Allowed: 1 to 32 chars (letters, numbers, dot, underscore, hyphen, space).", ssid));
				client->BadRequest(RESTAPI::Errors::SSIDInvalidName);
				return false;
			}
			return true;
		}

		static bool validatePassword(RESTAPIHandler *client, const std::string &password) {
			static const std::regex Pass_Regex(R"(^\S{8,32}$)");
			if (!std::regex_match(password, Pass_Regex)) {
				Poco::Logger::get("Configure").error("Invalid password. Must be 8 to 32 characters without spaces.");
				client->BadRequest(RESTAPI::Errors::SSIDInvalidPassword);
				return false;
			}
			return true;
		}

		/*
			SetInterfacesSsids():
			1. Read Body["ssid"] and extract required fields "name" and "password".
			2. Validate SSID name and password format.
			3. For each interface in Config["interfaces"], update non-mesh SSID entries:
			   - ssid["name"] = requested name
			   - ssid["encryption"]["key"] = requested password
			4. Return success if at least one SSID was updated; otherwise return error.
		*/
		static bool SetInterfacesSsids(RESTAPIHandler *client, Poco::JSON::Object::Ptr &Config, const std::string &SerialNumber, const Poco::JSON::Object::Ptr &Body) {
			std::string override_ssid{};
			std::string override_password{};

			/*
			  Request body shape:
				"ssid": {
					"name": " SSID_Name",
					"password": "SSID_Password"
				}
			*/

			auto ssidObj = Body->getObject("ssid");
			if (!ssidObj) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			if (!ssidObj->has("name") || !ssidObj->has("password")) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			try {
				override_ssid = ssidObj->getValue<std::string>("name");
				override_password = ssidObj->getValue<std::string>("password");
			} catch (...) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			Poco::trimInPlace(override_ssid);
			Poco::trimInPlace(override_password);
			if (override_ssid.empty() || override_password.empty()) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			if (!validateSsid(client, override_ssid) || !validatePassword(client, override_password)) {
				return false;
			}
			if (Config) {
				Poco::Logger::get("Configure").information(
					fmt::format("Applying SSID/Password overrides to device {}.", SerialNumber));
				try {
					auto interfaces = Config->getArray("interfaces");
					if (!interfaces || interfaces->size() == 0) {
						Poco::Logger::get("Configure").error(fmt::format("Missing or empty 'interfaces' for device {}.", SerialNumber));
						client->InternalError(RESTAPI::Errors::InternalError);
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
							if (ssid->has("bss-mode") && ssid->getValue<std::string>("bss-mode") == "mesh") {
								continue; //skipping ssid if bss-mode is mesh
							}
							auto encrypt = ssid->getObject("encryption");
							if (!encrypt || encrypt->size() == 0) {
								Poco::Logger::get("Configure").error(fmt::format("Missing or invalid 'encryption' for device {}.", SerialNumber));
								client->InternalError(RESTAPI::Errors::InternalError);
								return false;
							}
							ssid->set("name", override_ssid);
							Poco::Logger::get("Configure").information(fmt::format("Applied New SSID: [{}] on device {}.", override_ssid, SerialNumber));
							encrypt->set("key", override_password);
							Poco::Logger::get("Configure").information(fmt::format("Applied New Password for SSID: [{}] on device {}.", override_ssid, SerialNumber));
						}
					}
					Poco::Logger::get("Configure").information(fmt::format("SSID/Password overrides applied to device {}.", SerialNumber));
					if (!iface_ssids) {
						Poco::Logger::get("Configure").error(fmt::format("No SSID configuration found for device {}.", SerialNumber));
						client->InternalError(RESTAPI::Errors::InternalError);
						return false;
					}
				} catch (const std::exception &ex) {
					Poco::Logger::get("Configure").error(fmt::format("SetInterfaceSsid: exception for device {}: {}", SerialNumber, ex.what()));
					client->InternalError(RESTAPI::Errors::InternalError);
					return false;
				}
			}
			return true;
		}

		/*
			GetBlockedClients():
			1. Read Config["config-raw"] (if missing/empty, return an empty list).
			2. Walk firewall rule blocks (started by ["add","firewall","rule"]).
			3. Detect our block-clients rule by name "Block_Clients".
			4. Collect all src_mac entries from that rule into blockedMacs (normalized).
		*/
		static void GetBlockedClients(const Poco::JSON::Object::Ptr &Config, std::list<std::string> &blockedMacs) {
			Poco::Logger::get("SDK_gw").debug("Reading blocked client MACs from config-raw.");
			blockedMacs.clear();

			auto configRaw = Config->getArray("config-raw");
			if (!configRaw || configRaw->size() == 0)
			return;

			bool inFirewallRule = false;
			bool isBlockRule = false;

			for (std::size_t i = 0; i < configRaw->size(); ++i) {
				try {
					auto cmd = configRaw->getArray(i);
					if (!cmd || cmd->size() != 3)
						continue;

					auto op = cmd->getElement<std::string>(0);
					auto key = cmd->getElement<std::string>(1);
					auto val = cmd->getElement<std::string>(2);

					if (op == "add" && key == "firewall" && val == "rule") {
						inFirewallRule = true;
						isBlockRule = false;
						continue;
					}

					if (!inFirewallRule)
						continue;

					if (op == "set" && key == "firewall.@rule[-1].name") {
						isBlockRule = (val == "Block_Clients");
						continue;
					}

					if (isBlockRule && op == "add_list" && key == "firewall.@rule[-1].src_mac") {
						std::string mac = val;
						if (Utils::NormalizeMac(mac)) {
							blockedMacs.push_back(mac);
							Poco::Logger::get("SDK_gw").debug(fmt::format("Blocked client MAC found: {}", Utils::SerialToMAC(mac)));
						}
					}
				} catch (...) {
					continue;
				}
			}
		}

		/*
			makeRawCommand():
			1. Build a single config-raw command array with 3 elements: [op, path, value].
			2. Return it so callers can append it into a config-raw list.
		*/
		static Poco::JSON::Array::Ptr makeRawCommand(const std::string &op, const std::string &path, const std::string &value) {
			auto cmd = Poco::makeShared<Poco::JSON::Array>();
			cmd->add(op);
			cmd->add(path);
			cmd->add(value);
			return cmd;
		}

		/*
			buildBlockRuleConfigRaw():
			1. Build a config-raw array containing a single firewall rule named "Block_Clients".
			2. Add one src_mac entry per blocked MAC (from blockedMacsNorm).
			3. Return the constructed config-raw array so it can be saved into Config["config-raw"].
		*/
		static Poco::JSON::Array::Ptr buildBlockRuleConfigRaw(const std::list<std::string> &blockedMacsNorm) {
			auto raw = Poco::makeShared<Poco::JSON::Array>();

			raw->add(makeRawCommand("add", "firewall", "rule"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].name", "Block_Clients"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].src", "down1v0"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].dest", "up0v0"));

			for (const auto &macNorm : blockedMacsNorm) {
				raw->add(makeRawCommand("add_list", "firewall.@rule[-1].src_mac", Utils::SerialToMAC(macNorm)));
			}

			raw->add(makeRawCommand("set", "firewall.@rule[-1].family", "any"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].proto", "all"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].target", "REJECT"));
			raw->add(makeRawCommand("set", "firewall.@rule[-1].enabled", "1"));

			return raw;
		}

		/*
			BlockClient():
			1. Read and validate the request "client" array (each item needs "mac" + "access").
			2. Normalize MAC addresses and validate access is either "allow" or "deny".
			3. Parse current blocked client MACs from fetched config "config-raw" (Block_Clients rule).
			4. Apply allow/deny updates to the blocked list.
			5. Persist the updated blocked list back into Config["config-raw"] (or remove it if empty).
		*/
		static bool BlockClient(RESTAPIHandler *client, Poco::JSON::Object::Ptr &Config, const Poco::JSON::Object::Ptr &Body) {

			// 1) Fetch client list
			auto clientList = Body->getArray("client");
			if (!clientList || clientList->size() == 0) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			// 2) Validate request entries
			for (std::size_t i = 0; i < clientList->size(); ++i) {
				auto entry = clientList->getObject(i);
				if (!entry || !entry->has("mac") || !entry->has("access")) {
					client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
					return false;
				}

				std::string mac;
				std::string access;
				try {
					mac = entry->getValue<std::string>("mac");
					access = entry->getValue<std::string>("access");
				} catch (...) {
					client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
					return false;
				}

				Poco::trimInPlace(access);
				Poco::toLowerInPlace(access);

				if (!Utils::NormalizeMac(mac)) {
					client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
					return false;
				}

				if (access != "allow" && access != "deny") {
					client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
					return false;
				}
			}

			// 3) Read current blocked MACs from fetched config
			std::list<std::string> blockedMacs;
			GetBlockedClients(Config, blockedMacs);

			// 4) Apply allow/deny updates
			for (std::size_t i = 0; i < clientList->size(); ++i) {
				auto requestedClient = clientList->getObject(i);

				std::string requestedMac = requestedClient->getValue<std::string>("mac");
				std::string requestedAccess = requestedClient->getValue<std::string>("access");

				Poco::trimInPlace(requestedAccess);
				Poco::toLowerInPlace(requestedAccess);

				if (!Utils::NormalizeMac(requestedMac)) {
					client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
					return false;
				}

				bool alreadyBlocked = false;
				for (const auto &blockedMac : blockedMacs) {
					if (blockedMac == requestedMac) {
						alreadyBlocked = true;
						break;
					}
				}

				if (requestedAccess == "deny") {
					if (alreadyBlocked) {
						client->BadRequest(RESTAPI::Errors::ClientAlreadyBlocked);
						return false;
					}
					Poco::Logger::get("SDK_gw").information(fmt::format("Blocking client MAC: {}", requestedMac));
					blockedMacs.push_back(requestedMac); // block it
				} else { // access = allow
					if (!alreadyBlocked) {
						client->BadRequest(RESTAPI::Errors::ClientAlreadyUnblocked);
						return false;
					}
					Poco::Logger::get("SDK_gw").information(fmt::format("Allowing client MAC: {}", requestedMac));
					blockedMacs.remove(requestedMac); // unblock it
				}
			}

			// 5) Save updated blocked list back into config-raw
			if (blockedMacs.empty()) {
				Poco::Logger::get("SDK_gw").information("No blocked clients, removing config-raw.");
				if (Config->has("config-raw"))
					Config->remove("config-raw");
			} else {
				Poco::Logger::get("SDK_gw").information("Updating config-raw with blocked clients.");
				Config->set("config-raw", buildBlockRuleConfigRaw(blockedMacs));
			}

			return true;
		}

		/*
			SetConfig():
			1. Validate subscriber has at least one device and identify gateway serialNumber.
			2. Fetch the gateway's current configuration from the controller.
			3. Apply requested SSID updates (if "ssid" is present in the request body).
			4. Apply requested client block/unblock updates (if "client" is present in the request body).
			5. Send the updated config to the gateway device.
			6. If mesh devices exist and SSID changes were requested, build mesh config and push it to each mesh device.
		*/
		bool SetConfig(RESTAPIHandler *client, const Poco::JSON::Object::Ptr &Body, const Poco::SharedPtr<SubObjects::SubscriberInfo> &SubInfo) {

			if (!Body) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			if (SubInfo->accessPoints.list.empty()) {
				client->BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
				return false;
			}

			const auto &GatewaySerial = SubInfo->accessPoints.list.front().serialNumber;
			if (GatewaySerial.empty()) {
				client->BadRequest(RESTAPI::Errors::MissingSerialNumber);
				return false;
			}

			// 1) Fetch gateway config
			Poco::JSON::Object::Ptr DeviceObj;
			auto getStatus = GetConfig(client, GatewaySerial, DeviceObj);
			if (getStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				client->ForwardErrorResponse(client, getStatus, DeviceObj);
				return false;
			}

			if (!DeviceObj || !DeviceObj->has("configuration")) {
				client->BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				return false;
			}

			auto Config = DeviceObj->getObject("configuration");
			if (!Config) {
				client->InternalError(RESTAPI::Errors::InternalError);
				return false;
			}

			// 2) Apply SSID updates (only if requested)
			if (Body->has("ssid")) {
				if (!SetInterfacesSsids(client, Config, GatewaySerial, Body))
				return false;
			}

			// 3) Apply client block/unblock (only if requested)
			if (Body->has("client")) {
				if (!BlockClient(client, Config, Body)) {
					return false;
				}
			}

			// 4) Send config to gateway
			Poco::JSON::Object::Ptr gatewayResponse;
			if (!Configure(client, GatewaySerial, Config, gatewayResponse)) {
				client->ForwardErrorResponse(client, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, gatewayResponse);
				return false;
			}

			// 5) If only gateway, done
			if (SubInfo->accessPoints.list.size() <= 1)
				return true;

			// Mesh devices should be configured only when SSID changes are requested.
			if (!Body->has("ssid"))
				return true;

			Poco::Logger::get("SDK_gw").information("Preparing new configuration for mesh devices.");
			// 6) Build mesh config: force ipv4 dynamic + remove firewall config-raw commands.
			auto meshConfig = SDK::Prov::Subscriber::BuildMeshConfig(DeviceObj);
			if (!meshConfig) {
				client->InternalError(RESTAPI::Errors::InternalError);
				return false;
			}

			Poco::Logger::get("SDK_gw").information("Sending new configuration to mesh devices.");
			// 7) Send config to mesh devices
			for (size_t i = 1; i < SubInfo->accessPoints.list.size(); ++i) {
				const auto &meshSerial = SubInfo->accessPoints.list[i].serialNumber;

				Poco::JSON::Object::Ptr meshResponse;
				if (!Configure(client, meshSerial, meshConfig, meshResponse)) {
					client->ForwardErrorResponse(client, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, meshResponse);
					return false;
				}
			}
			return true;
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
