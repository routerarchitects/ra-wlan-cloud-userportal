/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//
#include "RESTAPI/RESTAPI_parental_control_utils.h"
#include "sdks/SDK_parental_control.h"
#include <Poco/DateTime.h>
#include <Poco/DateTimeFormatter.h>
#include <Poco/Timespan.h>
#include <algorithm>
#include <regex>
#include <sstream>
#include "framework/RESTAPI_Handler.h"
#include "RESTAPI/RESTAPI_topology_handler.h"

#include "ConfigMaker.h"
#include "SDK_gw.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/utils.h"

namespace OpenWifi::SDK::GW {
	namespace {
		bool ExecuteGatewayCommand(RESTAPIHandler *client, const std::string &endPoint,
								   Poco::JSON::Object &commandRequest,
								   Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
								   Poco::JSON::Object::Ptr &response) {
			auto API = OpenAPIRequestPost(uSERVICE_GATEWAY, endPoint, {}, commandRequest, 60000);
			Poco::JSON::Object::Ptr callResponse;
			responseStatus =
				API.Do(callResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
			response = callResponse ? callResponse : Poco::makeShared<Poco::JSON::Object>();
			return responseStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}
	} // namespace

	namespace Device {
		bool Reboot(RESTAPIHandler *client, const std::string &Mac,
					[[maybe_unused]] uint64_t When,
					Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					Poco::JSON::Object::Ptr &Response) {
			std::string EndPoint = "/api/v1/device/" + Mac + "/reboot";
			Poco::JSON::Object ObjRequest;

			ObjRequest.set("serialNumber", Mac);
			ObjRequest.set("when", 0);
			return ExecuteGatewayCommand(client, EndPoint, ObjRequest, ResponseStatus, Response);
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
			return ExecuteGatewayCommand(client, EndPoint, ObjRequest, ResponseStatus, Response);
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
			return ExecuteGatewayCommand(client, EndPoint, ObjRequest, ResponseStatus, Response);
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
			return ExecuteGatewayCommand(client, EndPoint, ObjRequest, ResponseStatus, Response);
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

		static std::string SerializeJson(const Poco::JSON::Object::Ptr &object) {
			if (!object) {
				return {};
			}
			std::ostringstream os;
			Poco::JSON::Stringifier::condense(object, os);
			return os.str();
		}

		/*
			ApplyClientAccessChanges():
			1. Read and validate the request "client" array (each item needs "mac" + "access").
			2. For each entry:
			   - If access == "deny": compute start/stop date/time (with optional duration) and call
				 SDK::ParentalControl::CreateClientAccess.
			   - If access == "allow": call SDK::ParentalControl::DeleteClientAccess.
			3. Extract config-raw snapshot from the PC response and update Config["config-raw"].
		*/
		static bool ApplyClientAccessChanges(RESTAPIHandler *client,
											 Poco::JSON::Object::Ptr &Config,
											 const Poco::JSON::Object::Ptr &Body,
											 const std::string &SubscriberId,
											 Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
											 Poco::JSON::Object::Ptr &response) {
			if (!Config || !Body || !Body->has("client") || !Body->isArray("client")) {
				Poco::Logger::get("SDK_gw").error("Missing or invalid 'client' array in request body.");
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			auto clientList = Body->getArray("client");
			if (!clientList || clientList->size() == 0) {
				Poco::Logger::get("SDK_gw").error("'client' array is empty.");
				return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
										RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
										response);
			}

			Poco::Logger::get("SDK_gw").information(fmt::format("Processing {} client access entry(ies) for subscriber: [{}]", clientList->size(), SubscriberId));

			Poco::JSON::Object::Ptr lastCallResponse;

			for (std::size_t i = 0; i < clientList->size(); ++i) {
				auto entry = clientList->getObject(i);
				if (!entry || !entry->has("mac") || !entry->has("access")) {
					Poco::Logger::get("SDK_gw").error(fmt::format("Entry {} missing required 'mac' or 'access' fields.", i));
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}

				std::string mac;
				std::string access;
				int64_t durationSec = 0;
				bool hasDuration = false;
				try {
					mac = entry->getValue<std::string>("mac");
					access = entry->getValue<std::string>("access");
					if (entry->has("duration") && !entry->isNull("duration")) {
						hasDuration = true;
						durationSec = entry->getValue<int64_t>("duration");
					}
				} catch (...) {
					Poco::Logger::get("SDK_gw").error(fmt::format("Failed to parse fields for entry {}.", i));
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}
				Poco::trimInPlace(access);
				Poco::toLowerInPlace(access);
				if (!Utils::NormalizeMac(mac) || (access != "allow" && access != "deny") || (hasDuration && durationSec < 1)) {
					Poco::Logger::get("SDK_gw").error(fmt::format("Invalid MAC [{}], access [{}], or duration [{}] for entry {}.", mac, access, durationSec, i));
					return SetErrorResponse(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
											RESTAPI::Errors::MissingOrInvalidParameters, responseStatus,
											response);
				}
				std::string formattedMac = Utils::SerialToMAC(mac);
				Poco::toLowerInPlace(formattedMac);

				Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
				Poco::JSON::Object::Ptr callResponse;

				if (access == "deny") {
					Poco::DateTime now;
					std::string startDate = Poco::DateTimeFormatter::format(now, "%Y-%m-%d");
					std::string startTime = Poco::DateTimeFormatter::format(now, "%H:%M:%S");

					Poco::DateTime nextDay = now + Poco::Timespan(86400, 0);
					std::string stopDate = Poco::DateTimeFormatter::format(nextDay, "%Y-%m-%d");

					std::string stopTime;
					if (durationSec > 0) {
						Poco::DateTime endDt = now + Poco::Timespan(durationSec, 0);
						stopTime = Poco::DateTimeFormatter::format(endDt, "%H:%M:%S");
					} else {
						stopTime = "23:59:59";
					}

					Poco::JSON::Object reqBody;
					reqBody.set("client_mac", formattedMac);
					reqBody.set("start_date", startDate);
					reqBody.set("stop_date", stopDate);
					reqBody.set("start_time", startTime);
					reqBody.set("stop_time", stopTime);

					Poco::Logger::get("SDK_gw").information(fmt::format(
						"Sending BLOCK request for client: [{}] subscriber: [{}] "
						"(start_date={}, stop_date={}, start_time={}, stop_time={}) to parental-control",
						formattedMac, SubscriberId, startDate, stopDate, startTime, stopTime));

					if (!SDK::ParentalControl::CreateClientAccess(client, SubscriberId, reqBody, callStatus, callResponse)) {
						Poco::Logger::get("SDK_gw").error(fmt::format("CreateClientAccess failed for client: [{}] subscriber: [{}], Status={}",
							formattedMac, SubscriberId, static_cast<int>(callStatus)));
						responseStatus = callStatus;
						response = callResponse ? callResponse : Poco::makeShared<Poco::JSON::Object>();
						return false;
					}
					lastCallResponse = callResponse;
				} else {
					std::string rawResponseBody;
					Poco::Logger::get("SDK_gw").information(fmt::format("Sending UNBLOCK request for client: [{}] subscriber: [{}] to parental-control",
									formattedMac, SubscriberId));

					if (!SDK::ParentalControl::DeleteClientAccess(client, SubscriberId, formattedMac, callStatus, callResponse, rawResponseBody)) {
						Poco::Logger::get("SDK_gw").error(fmt::format("DeleteClientAccess failed for client: [{}] subscriber: [{}], Status={}",
							formattedMac, SubscriberId, static_cast<int>(callStatus)));
						responseStatus = callStatus;
						response = callResponse ? callResponse : Poco::makeShared<Poco::JSON::Object>();
						return false;
					}
					lastCallResponse = callResponse;
				}
			}

			Poco::JSON::Array::Ptr configRaw;
			if (lastCallResponse && RESTAPI::ParentalControl::ExtractConfigRawSnapshot(lastCallResponse, configRaw, false) && configRaw && configRaw->size() > 0) {
				Poco::Logger::get("SDK_gw").information(fmt::format("Updated config-raw snapshot with {} rule(s) from parental-control", configRaw->size()));
				Config->set("config-raw", configRaw);
			} else {
				Poco::Logger::get("SDK_gw").information("No active parental control rules remaining, clearing config-raw section");
				if (Config->has("config-raw")) {
					Config->remove("config-raw");
				}
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
					   const std::string &GatewaySerial, const std::string &SubscriberId,
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
			const auto originalConfigSnapshot = SerializeJson(config);

			if (Body->has("ssid") &&
				!ApplySsidOverrides(config, GatewaySerial, Body, ResponseStatus, Response)) {
				return false;
			}

			if (Body->has("client") &&
				!ApplyClientAccessChanges(client, config, Body, SubscriberId, ResponseStatus, Response)) {
				return false;
			}
			const auto updatedConfigSnapshot = SerializeJson(config);
			if (updatedConfigSnapshot == originalConfigSnapshot) {
				ResponseStatus = Poco::Net::HTTPResponse::HTTP_OK;
				Response = Poco::makeShared<Poco::JSON::Object>();
				return true;
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
			Response = Poco::makeShared<Poco::JSON::Object>();

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
