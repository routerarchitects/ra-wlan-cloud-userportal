/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//
#include <algorithm>
#include <regex>
#include "framework/RESTAPI_Handler.h"

#include "ConfigMaker.h"
#include "SDK_gw.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/utils.h"

namespace OpenWifi::SDK::GW {
	namespace Device {
		bool Reboot(RESTAPIHandler *client, const std::string &Mac,
					[[maybe_unused]] uint64_t When,
					Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/reboot";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", 0);
			return ExecuteCommand(client, "reboot", EndPoint, ObjRequest, ResponseStatus, Response);
		}

		bool LEDs(RESTAPIHandler *client, const std::string &Mac, uint64_t When, uint64_t Duration,
				  const std::string &Pattern,
				  Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
				  Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/leds";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("duration", Duration);
			ObjRequest.set("pattern", Pattern);
			return ExecuteCommand(client, "leds", EndPoint, ObjRequest, ResponseStatus, Response);
		}

		bool Factory(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 bool KeepRedirector,
					 Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					 Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/factory";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("keepRedirector", KeepRedirector);
			return ExecuteCommand(client, "factory", EndPoint, ObjRequest, ResponseStatus, Response);
		}

		bool Upgrade(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 const std::string &ImageName, [[maybe_unused]] bool KeepRedirector,
					 Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					 Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/upgrade";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", When);
			ObjRequest.set("uri", ImageName);
			return ExecuteCommand(client, "upgrade", EndPoint, ObjRequest, ResponseStatus, Response);
		}

		bool ExecuteCommand(RESTAPIHandler *client, const std::string &Command,
							const std::string &EndPoint, Poco::JSON::Object &CommandRequest,
							Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
							Poco::JSON::Object::Ptr &Response) {
			auto API = OpenAPIRequestPost(uSERVICE_GATEWAY, EndPoint, {}, CommandRequest, 60000);
			Poco::JSON::Object::Ptr callResponse;
			ResponseStatus =
				API.Do(callResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (!callResponse)
				callResponse = Poco::makeShared<Poco::JSON::Object>();

			Response = Poco::makeShared<Poco::JSON::Object>();
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT) {
				Response->set("Code", Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT);
				Response->set(
					"Details",
					"Command could not complete, you may want to retry this operation later.");
				Response->set("Operation", Command);
				return false;
			}

			if (callResponse->has("command") && callResponse->has("errorCode") &&
				callResponse->has("errorText")) {
				Response->set("Operation", callResponse->get("command").toString());
				Response->set("Details", callResponse->get("errorText").toString());
				Response->set("Code", callResponse->get("errorCode"));
			} else {
				Response = callResponse;
			}
			return ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
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

		bool GetConfig(RESTAPIHandler *client, const std::string &Mac,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac;
			auto API = OpenAPIRequestGet(uSERVICE_GATEWAY, EndPoint, {}, 1000);
			ResponseStatus =
				API.Do(Response, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Poco::Logger::get("SDK_gw").error(fmt::format(
					"GetConfig: Could not get configuration from controller for device id {}. "
					"Status={}",
					Mac, int(ResponseStatus)));
				return false;
			}
			if (!Response || !Response->has("configuration")) {
				Poco::Logger::get("SDK_gw").error(fmt::format(
					"GetConfig: Could not get configuration from controller for device id {}.", Mac));
				return false;
			}
			return true;
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

		bool Configure(RESTAPIHandler *client, const std::string &Mac,
					   Poco::JSON::Object::Ptr &Configuration,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response) {

			Poco::JSON::Object Body;
			uint64_t Now = Utils::Now();

			Configuration->set("uuid", Now);
			Body.set("serialNumber", Mac);
			Body.set("UUID", Now);
			Body.set("when", 0);
			Body.set("configuration", Configuration);

			OpenWifi::OpenAPIRequestPost R(OpenWifi::uSERVICE_GATEWAY,
										   "/api/v1/device/" + Mac + "/configure", {}, Body, 90000);

			ResponseStatus = R.Do(Response, client ? client->UserInfo_.webtoken.access_token_ : "");
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
		static bool SetErrorResponse(
			Poco::Net::HTTPResponse::HTTPStatus status, const OpenWifi::RESTAPI::Errors::msg &error,
			Poco::Net::HTTPResponse::HTTPStatus &responseStatus, Poco::JSON::Object::Ptr &response,
			const std::string &extra = "") {
			responseStatus = status;
			response = Poco::makeShared<Poco::JSON::Object>();
			response->set("ErrorCode", static_cast<uint32_t>(status));
			if (extra.empty()) {
				response->set("ErrorDescription", fmt::format("{}: {}", error.err_num, error.err_txt));
			} else {
				response->set("ErrorDescription",
							  fmt::format("{}: {} ({})", error.err_num, error.err_txt, extra));
			}
			return false;
		}

		static bool ValidateSsidName(
			std::string &ssid, Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
			Poco::JSON::Object::Ptr &response) {
			Poco::trimInPlace(ssid);
			static const std::regex kSsidRegex(R"(^[A-Za-z0-9._ \-]{1,32}$)");
			if (!std::regex_match(ssid, kSsidRegex)) {
				Poco::Logger::get("Configure")
					.error(fmt::format("Invalid SSID [{}].", ssid));
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::SSIDInvalidName, responseStatus, response);
			}
			return true;
		}

		static bool ValidateSsidPassword(
			const std::string &password, Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
			Poco::JSON::Object::Ptr &response) {
			static const std::regex kPasswordRegex(R"(^\S{8,32}$)");
			if (!std::regex_match(password, kPasswordRegex)) {
				Poco::Logger::get("Configure")
					.error("Invalid SSID password. Must be 8-32 chars without spaces.");
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::SSIDInvalidPassword, responseStatus, response);
			}
			return true;
		}

		/*
			ApplySsidOverrides():
			1. Read Body["ssid"] and extract required fields "name" and "password".
			2. Validate SSID name and password format.
			3. For each interface in Config["interfaces"], update non-mesh SSID entries:
			   - ssid["name"] = requested name
			   - ssid["encryption"]["key"] = requested password
			4. Return success if at least one SSID was updated; otherwise return error.
		*/
		static bool ApplySsidOverrides(
			Poco::JSON::Object::Ptr &Config, const std::string &SerialNumber,
			const Poco::JSON::Object::Ptr &Body,
			Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
			Poco::JSON::Object::Ptr &response) {
			if (!Config || !Body || !Body->has("ssid") || !Body->isObject("ssid")) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			std::string overrideSsid;
			std::string overridePassword;
			try {
				auto ssidObj = Body->getObject("ssid");
				if (!ssidObj || !ssidObj->has("name") || !ssidObj->has("password")) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}
				overrideSsid = ssidObj->getValue<std::string>("name");
				overridePassword = ssidObj->getValue<std::string>("password");
			} catch (...) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			Poco::trimInPlace(overrideSsid);
			Poco::trimInPlace(overridePassword);
			if (overrideSsid.empty() || overridePassword.empty()) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			if (!ValidateSsidName(overrideSsid, responseStatus, response) ||
				!ValidateSsidPassword(overridePassword, responseStatus, response)) {
				return false;
			}

			try {
				auto interfaces = Config->getArray("interfaces");
				if (!interfaces || interfaces->size() == 0) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
											RESTAPI::Errors::InternalError, responseStatus, response);
				}

				bool hasSsidEntries = false;
				for (std::size_t i = 0; i < interfaces->size(); ++i) {
					auto iface = interfaces->getObject(i);
					if (!iface || !iface->has("ssids") || !iface->isArray("ssids")) {
						continue;
					}
					auto ssids = iface->getArray("ssids");
					if (!ssids || ssids->size() == 0) {
						continue;
					}
					hasSsidEntries = true;

					for (std::size_t j = 0; j < ssids->size(); ++j) {
						auto ssid = ssids->getObject(j);
						if (!ssid) {
							continue;
						}
						if (ssid->has("bss-mode") && ssid->get("bss-mode").isString() &&
							ssid->getValue<std::string>("bss-mode") == "mesh") {
							continue;
						}
						if (!ssid->has("encryption") || !ssid->isObject("encryption")) {
							return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
													RESTAPI::Errors::InternalError, responseStatus,
													response);
						}
						auto encryption = ssid->getObject("encryption");
						if (!encryption) {
							return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
													RESTAPI::Errors::InternalError, responseStatus,
													response);
						}
						ssid->set("name", overrideSsid);
						encryption->set("key", overridePassword);
					}
				}

				if (!hasSsidEntries) {
					Poco::Logger::get("Configure")
						.error(fmt::format("No SSID entries available for device {}.", SerialNumber));
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
											RESTAPI::Errors::InternalError, responseStatus, response);
				}
			} catch (const std::exception &ex) {
				Poco::Logger::get("Configure")
					.error(fmt::format("ApplySsidOverrides failed for device {}: {}",
									   SerialNumber, ex.what()));
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
										RESTAPI::Errors::InternalError, responseStatus, response);
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
		static void CollectBlockedClients(const Poco::JSON::Object::Ptr &Config,
										  std::list<std::string> &blockedMacs) {
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
			MakeConfigRawCmd():
			1. Build a single config-raw command array with 3 elements: [op, path, value].
			2. Return it so callers can append it into a config-raw list.
		*/
		static Poco::JSON::Array::Ptr BuildConfigRawCommand(const std::string &op,
															const std::string &path,
															const std::string &value) {
			auto cmd = Poco::makeShared<Poco::JSON::Array>();
			cmd->add(op);
			cmd->add(path);
			cmd->add(value);
			return cmd;
		}

		/*
			MakeBlockRuleRaw():
			1. Build a config-raw array containing a single firewall rule named "Block_Clients".
			2. Add one src_mac entry per blocked MAC (from blockedMacsNorm).
			3. Return the constructed config-raw array so it can be saved into Config["config-raw"].
		*/
		static Poco::JSON::Array::Ptr BuildBlockClientsRule(
			const std::list<std::string> &blockedMacsNorm) {
			auto raw = Poco::makeShared<Poco::JSON::Array>();

			raw->add(BuildConfigRawCommand("add", "firewall", "rule"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].name", "Block_Clients"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].src", "down1v0"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].dest", "up0v0"));

			for (const auto &macNorm : blockedMacsNorm) {
				raw->add(BuildConfigRawCommand("add_list", "firewall.@rule[-1].src_mac",
											   Utils::SerialToMAC(macNorm)));
			}

			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].family", "any"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].proto", "all"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].target", "REJECT"));
			raw->add(BuildConfigRawCommand("set", "firewall.@rule[-1].enabled", "1"));

			return raw;
		}

		/*
			ApplyClientAccessChanges():
			1. Read and validate the request "client" array (each item needs "mac" + "access").
			2. Normalize MAC addresses and validate access is either "allow" or "deny".
			3. Parse current blocked client MACs from fetched config "config-raw" (Block_Clients rule).
			4. Apply allow/deny updates to the blocked list.
			5. Persist the updated blocked list back into Config["config-raw"] (or remove it if empty).
		*/
		static bool ApplyClientAccessChanges(
			Poco::JSON::Object::Ptr &Config, const Poco::JSON::Object::Ptr &Body,
			Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
			Poco::JSON::Object::Ptr &response) {
			if (!Config || !Body || !Body->has("client") || !Body->isArray("client")) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			auto clientList = Body->getArray("client");
			if (!clientList || clientList->size() == 0) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			for (std::size_t i = 0; i < clientList->size(); ++i) {
				auto entry = clientList->getObject(i);
				if (!entry || !entry->has("mac") || !entry->has("access")) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}

				std::string mac;
				std::string access;
				try {
					mac = entry->getValue<std::string>("mac");
					access = entry->getValue<std::string>("access");
				} catch (...) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}
				Poco::trimInPlace(access);
				Poco::toLowerInPlace(access);
				if (!Utils::NormalizeMac(mac) || (access != "allow" && access != "deny")) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}
			}

			std::list<std::string> blockedMacs;
			CollectBlockedClients(Config, blockedMacs);

			for (std::size_t i = 0; i < clientList->size(); ++i) {
				auto requestedClient = clientList->getObject(i);
				std::string requestedMac = requestedClient->getValue<std::string>("mac");
				std::string requestedAccess = requestedClient->getValue<std::string>("access");
				Poco::trimInPlace(requestedAccess);
				Poco::toLowerInPlace(requestedAccess);
				if (!Utils::NormalizeMac(requestedMac)) {
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}

				bool alreadyBlocked = std::find(blockedMacs.begin(), blockedMacs.end(),
												requestedMac) != blockedMacs.end();
				if (requestedAccess == "deny") {
					if (alreadyBlocked) {
						return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
												RESTAPI::Errors::ClientAlreadyBlocked, responseStatus,
												response);
					}
					blockedMacs.push_back(requestedMac);
				} else {
					if (!alreadyBlocked) {
						return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
												RESTAPI::Errors::ClientAlreadyUnblocked, responseStatus,
												response);
					}
					blockedMacs.remove(requestedMac);
				}
			}

			if (blockedMacs.empty()) {
				if (Config->has("config-raw")) {
					Config->remove("config-raw");
				}
			} else {
				Config->set("config-raw", BuildBlockClientsRule(blockedMacs));
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
		static bool IsSameSerial(const std::string &lhs, const std::string &rhs) {
			std::string normalizedLhs = lhs;
			std::string normalizedRhs = rhs;
			if (Utils::NormalizeMac(normalizedLhs) && Utils::NormalizeMac(normalizedRhs)) {
				return normalizedLhs == normalizedRhs;
			}
			return lhs == rhs;
		}

		bool SetConfig(RESTAPIHandler *client, const Poco::JSON::Object::Ptr &Body,
					   const ProvObjects::SubscriberDeviceList &SubscriberDevices,
					   const std::string &GatewaySerial,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response) {
			if (!Body) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, ResponseStatus,
										Response);
			}
			if (SubscriberDevices.subscriberDevices.empty()) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::SubNoDeviceActivated, ResponseStatus,
										Response);
			}
			if (GatewaySerial.empty()) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingSerialNumber, ResponseStatus,
										Response);
			}

			Poco::JSON::Object::Ptr deviceObj;
			Poco::Net::HTTPResponse::HTTPStatus getStatus =
				Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
			if (!GetConfig(client, GatewaySerial, getStatus, deviceObj)) {
				ResponseStatus = getStatus;
				Response = deviceObj ? deviceObj : Poco::makeShared<Poco::JSON::Object>();
				return false;
			}

			if (!deviceObj || !deviceObj->has("configuration") || !deviceObj->isObject("configuration")) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, ResponseStatus,
										Response);
			}
			auto config = deviceObj->getObject("configuration");
			if (!config) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
										RESTAPI::Errors::InternalError, ResponseStatus, Response);
			}

			if (Body->has("ssid") &&
				!ApplySsidOverrides(config, GatewaySerial, Body, ResponseStatus, Response)) {
				return false;
			}

			if (Body->has("client") &&
				!ApplyClientAccessChanges(config, Body, ResponseStatus, Response)) {
				return false;
			}

			Poco::JSON::Object::Ptr gatewayResponse;
			Poco::Net::HTTPResponse::HTTPStatus gatewayStatus =
				Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
			if (!Configure(client, GatewaySerial, config, gatewayStatus, gatewayResponse)) {
				ResponseStatus = gatewayStatus;
				if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
					ResponseStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
				}
				Response = gatewayResponse ? gatewayResponse : Poco::makeShared<Poco::JSON::Object>();
				return false;
			}

			ResponseStatus = Poco::Net::HTTPResponse::HTTP_OK;
			Response = gatewayResponse ? gatewayResponse : Poco::makeShared<Poco::JSON::Object>();

			if (!Body->has("ssid") || SubscriberDevices.subscriberDevices.size() <= 1) {
				return true;
			}

			Poco::JSON::Object::Ptr meshConfig;
			ConfigMaker configMaker(Poco::Logger::get("SDK_gw"));
			if (!configMaker.BuildMeshConfig(deviceObj, meshConfig)) {
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR,
										RESTAPI::Errors::InternalError, ResponseStatus, Response);
			}

			for (const auto &subscriberDevice : SubscriberDevices.subscriberDevices) {
				const auto &meshSerial = subscriberDevice.serialNumber;
				if (meshSerial.empty() || IsSameSerial(meshSerial, GatewaySerial)) {
					continue;
				}

				Poco::JSON::Object::Ptr meshResponse;
				Poco::Net::HTTPResponse::HTTPStatus meshStatus =
					Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
				if (!Configure(client, meshSerial, meshConfig, meshStatus, meshResponse)) {
					ResponseStatus = meshStatus;
					if (ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
						ResponseStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
					}
					Response = meshResponse ? meshResponse : Poco::makeShared<Poco::JSON::Object>();
					return false;
				}
			}
			return true;
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
	} // namespace Device
} // namespace OpenWifi::SDK::GW
