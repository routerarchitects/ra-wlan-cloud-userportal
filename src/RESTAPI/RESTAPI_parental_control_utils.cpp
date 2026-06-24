/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_parental_control_utils.h"
#include "Poco/Format.h"
#include "fmt/format.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_nw_topology.h"
#include "framework/utils.h"
#include "framework/ow_constants.h"
#include <cctype>
#include <set>
#include <vector>

namespace OpenWifi::RESTAPI::ParentalControl {

	namespace {
		bool MinuteToTimeString(int minuteOfDay, std::string &timeValue) {
			if (minuteOfDay < 0 || minuteOfDay > 1439) {
				return false;
			}

			const int hour = minuteOfDay / 60;
			const int minute = minuteOfDay % 60;
			timeValue = Poco::format("%02d:%02d", hour, minute);
			return true;
		}
	} // namespace

	bool NormalizeScheduleResponse(Poco::JSON::Object::Ptr schedule) {
		if (!schedule || !schedule->has("start_minute") || !schedule->has("stop_minute")) {
			return false;
		}

		int startMinute = 0;
		int stopMinute = 0;
		try {
			startMinute = schedule->getValue<int>("start_minute");
			stopMinute = schedule->getValue<int>("stop_minute");
		} catch (...) {
			return false;
		}

		std::string startTime;
		std::string stopTime;
		if (!MinuteToTimeString(startMinute, startTime) ||
			!MinuteToTimeString(stopMinute, stopTime)) {
			return false;
		}

		schedule->set("start_time", startTime);
		schedule->set("stop_time", stopTime);
		schedule->remove("start_minute");
		schedule->remove("stop_minute");
		schedule->remove("config-raw");
		return true;
	}

	bool ParseTimeString(const Poco::Dynamic::Var &value, int &minuteOfDay) {
		if (!value.isString()) {
			return false;
		}

		const auto timeValue = value.convert<std::string>();
		if (timeValue.size() != 5 || timeValue[2] != ':' ||
			!std::isdigit(static_cast<unsigned char>(timeValue[0])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[1])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[3])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[4]))) {
			return false;
		}

		const int hour = (timeValue[0] - '0') * 10 + (timeValue[1] - '0');
		const int minute = (timeValue[3] - '0') * 10 + (timeValue[4] - '0');
		if (hour > 23 || minute > 59) {
			return false;
		}

		minuteOfDay = (hour * 60) + minute;
		return true;
	}

	bool ValidateWeekdays(const Poco::JSON::Array::Ptr &weekdays) {
		if (!weekdays || weekdays->size() == 0) {
			return false;
		}

		std::set<int> seenDays;
		for (std::size_t i = 0; i < weekdays->size(); ++i) {
			try {
				const int day = weekdays->getElement<int>(i);
				if (day < 0 || day > 6 || !seenDays.insert(day).second) {
					return false;
				}
			} catch (...) {
				return false;
			}
		}
		return true;
	}

	bool ParseAndValidateScheduleRequest(RESTAPIHandler &handler,
										  const Poco::JSON::Object::Ptr &body,
										  bool enabledRequired,
										  ParsedScheduleRequest &out) {
		std::vector<std::string> names;
		body->getNames(names);
		for (const auto &name : names) {
			if (name != "name" && name != "description" && name != "enabled" &&
				name != "action_type" && name != "target_kind" && name != "target_value" &&
				name != "start_time" && name != "stop_time" && name != "weekdays") {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "Unknown field: " + name);
				return false;
			}
		}

		if (!body->has("name") || body->isNull("name") || !body->get("name").isString()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "name is required");
			return false;
		}
		out.name = body->getValue<std::string>("name");
		Poco::trimInPlace(out.name);
		if (out.name.empty()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "name must be non-empty");
			return false;
		}

		if (!body->has("action_type") || body->isNull("action_type") ||
			!body->get("action_type").isString() ||
			body->getValue<std::string>("action_type") != "BLOCK") {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "action_type must be BLOCK");
			return false;
		}

		if (!body->has("target_kind") || body->isNull("target_kind") ||
			!body->get("target_kind").isString()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "target_kind is required");
			return false;
		}
		out.targetKind = body->getValue<std::string>("target_kind");
		if (out.targetKind != "INTERNET" && out.targetKind != "APP") {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "target_kind must be INTERNET or APP");
			return false;
		}

		if (out.targetKind == "APP") {
			if (!body->has("target_value") || body->isNull("target_value") || !body->get("target_value").isString()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "APP schedules require a non-empty target_value");
				return false;
			}
			out.targetValue = body->getValue<std::string>("target_value");
			Poco::trimInPlace(out.targetValue);
			if (out.targetValue.empty()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "APP schedules require a non-empty target_value");
				return false;
			}
		} else {
			if (body->has("target_value")) {
				if (!body->isNull("target_value")) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "INTERNET schedules require target_value to be null");
					return false;
				}
			}
			out.targetValue = "";
		}

		if (!body->has("start_time") ||
			!ParseTimeString(body->get("start_time"), out.startMinute)) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "start_time must use HH:MM format");
			return false;
		}
		if (!body->has("stop_time") ||
			!ParseTimeString(body->get("stop_time"), out.stopMinute)) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "stop_time must use HH:MM format");
			return false;
		}
		if (out.startMinute == out.stopMinute) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "start_time and stop_time must not represent the same minute");
			return false;
		}

		if (!body->has("weekdays") || !body->isArray("weekdays") ||
			!ValidateWeekdays(body->getArray("weekdays"))) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "weekdays must contain distinct values in the range 0..6");
			return false;
		}
		out.weekdays = body->getArray("weekdays");

		if (enabledRequired) {
			if (!body->has("enabled") || body->isNull("enabled") ||
				body->get("enabled").type() != typeid(bool)) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "enabled is required and must be a boolean");
				return false;
			}
			out.enabled = body->getValue<bool>("enabled");
		} else {
			if (body->has("enabled")) {
				if (body->isNull("enabled") || body->get("enabled").type() != typeid(bool)) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "enabled must be a boolean");
					return false;
				}
				out.enabled = body->getValue<bool>("enabled");
			} else {
				out.enabled = true;
			}
		}

		if (body->has("description")) {
			if (body->isNull("description")) {
				out.description = std::nullopt;
			} else {
				if (!body->get("description").isString()) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "description must be a string or null");
					return false;
				}
				std::string desc = body->getValue<std::string>("description");
				Poco::trimInPlace(desc);
				out.description = std::move(desc);
			}
		} else {
			out.description = std::nullopt;
		}

		return true;
	}

	Poco::JSON::Object BuildScheduleRequestBody(const ParsedScheduleRequest &req) {
		Poco::JSON::Object body;
		body.set("name", req.name);
		if (req.description.has_value()) {
			body.set("description", *req.description);
		} else {
			body.set("description", Poco::Dynamic::Var());
		}
		body.set("enabled", req.enabled);
		body.set("action_type", "BLOCK");
		body.set("target_kind", req.targetKind);
		if (req.targetKind == "APP") {
			body.set("target_value", req.targetValue);
		} else {
			body.set("target_value", Poco::Dynamic::Var());
		}
		body.set("start_minute", req.startMinute);
		body.set("stop_minute", req.stopMinute);
		body.set("weekdays", req.weekdays);
		return body;
	}

	bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &callResponse,
								  Poco::JSON::Array::Ptr &configRaw,
								  bool required) {
		configRaw.reset();
		if (!callResponse) {
			return !required;
		}
		if (!callResponse->has("config-raw")) {
			return !required;
		}
		if (callResponse->isNull("config-raw")) {
			return true;
		}
		if (!callResponse->isArray("config-raw")) {
			return false;
		}
		configRaw = callResponse->getArray("config-raw");
		if (!configRaw) {
			return false;
		}
		for (std::size_t i = 0; i < configRaw->size(); ++i) {
			try {
				auto cmd = configRaw->getArray(i);
				if (!cmd) {
					return false;
				}
				if (cmd->size() != 2 && cmd->size() != 3) {
					return false;
				}
				for (std::size_t j = 0; j < cmd->size(); ++j) {
					if (!cmd->get(j).isString()) {
						return false;
					}
				}
			} catch (...) {
				return false;
			}
		}
		return true;
	}

	ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &handler, Poco::Logger &logger,
										const std::string &subscriberId,
										const std::string &operatorId, const std::string &objectId,
										const Poco::JSON::Array::Ptr &configRaw,
										const std::string &operationName,
										const std::string &objectType,
										const std::string &gatewaySerial) {
		if (!configRaw) {
			return ApplyConfigRawResult::NoConfigApplyNeeded;
		}

		if (operatorId.empty()) {
			logger.error(fmt::format("{}: operator id missing for gateway apply (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingOperatorId;
		}

		std::string resolvedSerial = gatewaySerial;
		if (resolvedSerial.empty()) {
			ProvObjects::SubscriberDeviceList devList;
			Poco::Net::HTTPResponse::HTTPStatus provStatus;
			Poco::JSON::Object::Ptr provResponse;
			if (!SDK::Prov::Subscriber::GetDevices(&handler, subscriberId, operatorId, devList, provStatus, provResponse)) {
				logger.error(fmt::format("{}: provisioning lookup failed (subscriber={} {}={})",
										 operationName, subscriberId, objectType, objectId));
				return ApplyConfigRawResult::ProvisioningLookupFailed;
			}

			for (const auto &dev : devList.subscriberDevices) {
				std::string grp = dev.deviceGroup;
				Poco::toLowerInPlace(grp);
				if (grp == "olg") {
					resolvedSerial = dev.serialNumber;
					break;
				}
			}
		}

		if (resolvedSerial.empty()) {
			logger.error(fmt::format("{}: gateway serial not resolved (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingGatewaySerial;
		}

		Poco::JSON::Object::Ptr gwResponse;
		Poco::Net::HTTPResponse::HTTPStatus gwStatus;
		if (!SDK::GW::Device::GetConfig(&handler, resolvedSerial, gwStatus, gwResponse)) {
			if (gwStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
										 resolvedSerial));
			} else {
				logger.error(fmt::format("{}: gateway config load failed (serial={})",
										 operationName, resolvedSerial));
			}
			return ApplyConfigRawResult::GatewayConfigLoadFailed;
		}

		if (!gwResponse || !gwResponse->has("configuration") ||
			!gwResponse->isObject("configuration")) {
			logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
									 resolvedSerial));
			return ApplyConfigRawResult::GatewayConfigMalformed;
		}

		auto gatewayConfig = gwResponse->getObject("configuration");
		// Replacing the full config-raw section is intentional because parental-control is
		// currently the only service producing config-raw, and the gateway-fetched config-raw
		// doesn't include a reliable ownership marker to enable selective merging.
		gatewayConfig->set("config-raw", configRaw);

		Poco::JSON::Object::Ptr configureResponse;
		Poco::Net::HTTPResponse::HTTPStatus configureStatus;
		if (!SDK::GW::Device::Configure(&handler, resolvedSerial, gatewayConfig, configureStatus,
										configureResponse)) {
			logger.error(fmt::format("{}: gateway configure failed (serial={})", operationName,
									 resolvedSerial));
			return ApplyConfigRawResult::GatewayConfigureFailed;
		}

		return ApplyConfigRawResult::Applied;
	}

	bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result) {
		switch (result) {
		case ApplyConfigRawResult::NoConfigApplyNeeded:
		case ApplyConfigRawResult::Applied:
			return true;
		case ApplyConfigRawResult::MissingOperatorId:
			handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
			return false;
		case ApplyConfigRawResult::MissingGatewaySerial:
			handler.InternalError(RESTAPI::Errors::MissingSerialNumber);
			return false;
		case ApplyConfigRawResult::ProvisioningLookupFailed:
		case ApplyConfigRawResult::GatewayConfigLoadFailed:
		case ApplyConfigRawResult::GatewayConfigMalformed:
		case ApplyConfigRawResult::GatewayConfigureFailed:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		handler.InternalError(RESTAPI::Errors::InternalError);
		return false;
	}

	ValidateMacResult ValidateMacInTopology(RESTAPIHandler &handler,
											const std::string &subscriberId,
											const std::string &operatorId,
											const std::string &clientMac,
											std::string &gatewaySerial) {
		if (subscriberId.empty() || operatorId.empty()) {
			return ValidateMacResult::MissingSubscriberOrOperator;
		}

		ProvObjects::SubscriberDeviceList devList;
		Poco::Net::HTTPResponse::HTTPStatus provStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr provResponse;
		if (!SDK::Prov::Subscriber::GetDevices(&handler, subscriberId, operatorId, devList,
											   provStatus, provResponse)) {
			if (provStatus == Poco::Net::HTTPResponse::HTTP_NOT_FOUND) {
				return ValidateMacResult::SubscriberDevicesNotFound;
			}
			return ValidateMacResult::ProvisioningLookupFailed;
		}

		if (devList.subscriberDevices.empty()) {
			return ValidateMacResult::SubscriberDevicesNotFound;
		}

		for (const auto &dev : devList.subscriberDevices) {
			std::string grp = dev.deviceGroup;
			Poco::toLowerInPlace(grp);
			if (grp == "olg") {
				gatewaySerial = dev.serialNumber;
				break;
			}
		}

		if (gatewaySerial.empty()) {
			return ValidateMacResult::GatewaySerialNotFound;
		}

		ProvObjects::InventoryTag inventory;
		if (!SDK::Prov::Device::Get(&handler, gatewaySerial, inventory)) {
			return ValidateMacResult::InventoryNotFound;
		}

		if (inventory.venue.empty()) {
			return ValidateMacResult::VenueNotFound;
		}

		Poco::Net::HTTPServerResponse::HTTPStatus venueStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr venueResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::Venue venue;
		if (!SDK::Prov::Venue::Get(&handler, inventory.venue, venue, venueStatus, venueResponse)) {
			return ValidateMacResult::VenueNotFound;
		}

		if (venue.boards.empty() || venue.boards.front().empty()) {
			return ValidateMacResult::BoardIdNotFound;
		}

		std::string boardId = venue.boards.front();

		Poco::Net::HTTPServerResponse::HTTPStatus topoStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		Poco::JSON::Object::Ptr topologyResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Topology::Get(&handler, boardId, topoStatus, topologyResponse)) {
			return ValidateMacResult::TopologyNotFound;
		}

		if (!topologyResponse) {
			return ValidateMacResult::TopologyNotFound;
		}

		// Check structures
		bool hasNodes = topologyResponse->has("nodes");
		bool hasHistClients = topologyResponse->has("historicalClients");
		bool hasHistDevs = topologyResponse->has("historicalDevices");

		if ((hasNodes && !topologyResponse->isArray("nodes")) ||
			(hasHistClients && !topologyResponse->isArray("historicalClients")) ||
			(hasHistDevs && !topologyResponse->isArray("historicalDevices")) ||
			(!hasNodes && !hasHistClients && !hasHistDevs)) {
			return ValidateMacResult::TopologyUnusable;
		}

		// Normalize client MAC to search
		std::string targetMac = clientMac;
		if (!Utils::NormalizeMac(targetMac)) {
			return ValidateMacResult::MacNotPresentInTopology;
		}
		std::string targetMacFormatted = Utils::SerialToMAC(targetMac);
		Poco::toLowerInPlace(targetMacFormatted);

		// Check historicalClients
		if (hasHistClients) {
			auto histClients = topologyResponse->getArray("historicalClients");
			for (std::size_t i = 0; i < histClients->size(); ++i) {
				auto item = histClients->getObject(i);
				if (!item) {
					return ValidateMacResult::TopologyUnusable;
				}
				if (item->has("station") && item->get("station").isString()) {
					std::string station = item->getValue<std::string>("station");
					Poco::toLowerInPlace(station);
					if (station == targetMacFormatted) {
						return ValidateMacResult::Success;
					}
				}
			}
		}

		// Check historicalDevices (array of MAC strings)
		if (hasHistDevs) {
			auto histDevs = topologyResponse->getArray("historicalDevices");
			for (std::size_t i = 0; i < histDevs->size(); ++i) {
				try {
					if (!histDevs->get(i).isString()) {
						return ValidateMacResult::TopologyUnusable;
					}
					std::string station = histDevs->getElement<std::string>(i);
					if (Utils::NormalizeMac(station)) {
						std::string normStation = Utils::SerialToMAC(station);
						Poco::toLowerInPlace(normStation);
						if (normStation == targetMacFormatted) {
							return ValidateMacResult::Success;
						}
					}
				} catch (...) {
					return ValidateMacResult::TopologyUnusable;
				}
			}
		}

		// Check active nodes / aps / clients
		if (hasNodes) {
			auto nodes = topologyResponse->getArray("nodes");
			for (std::size_t i = 0; i < nodes->size(); ++i) {
				auto node = nodes->getObject(i);
				if (!node) {
					return ValidateMacResult::TopologyUnusable;
				}
				if (node->has("aps")) {
					if (!node->isArray("aps")) {
						return ValidateMacResult::TopologyUnusable;
					}
					auto aps = node->getArray("aps");
					for (std::size_t apIndex = 0; apIndex < aps->size(); ++apIndex) {
						auto ap = aps->getObject(apIndex);
						if (!ap) {
							return ValidateMacResult::TopologyUnusable;
						}
						if (ap->has("clients")) {
							if (!ap->isArray("clients")) {
								return ValidateMacResult::TopologyUnusable;
							}
							auto clients = ap->getArray("clients");
							for (std::size_t clientIndex = 0; clientIndex < clients->size(); ++clientIndex) {
								auto client = clients->getObject(clientIndex);
								if (!client) {
									return ValidateMacResult::TopologyUnusable;
								}
								if (client->has("station")) {
									if (!client->get("station").isString()) {
										return ValidateMacResult::TopologyUnusable;
									}
									std::string station = client->getValue<std::string>("station");
									Poco::toLowerInPlace(station);
									if (station == targetMacFormatted) {
										return ValidateMacResult::Success;
									}
								}
							}
						}
					}
				}
			}
		}

		return ValidateMacResult::MacNotPresentInTopology;
	}

	bool HandleValidateMacResult(RESTAPIHandler &handler, ValidateMacResult result) {
		switch (result) {
		case ValidateMacResult::Success:
			return true;
		case ValidateMacResult::MissingSubscriberOrOperator:
			handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
			return false;
		case ValidateMacResult::SubscriberDevicesNotFound:
		case ValidateMacResult::GatewaySerialNotFound:
			handler.BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		case ValidateMacResult::ProvisioningLookupFailed:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		case ValidateMacResult::InventoryNotFound:
			handler.BadRequest(RESTAPI::Errors::GatewayInventoryNotFound);
			return false;
		case ValidateMacResult::VenueNotFound:
			handler.BadRequest(RESTAPI::Errors::VenueMustExist);
			return false;
		case ValidateMacResult::BoardIdNotFound:
			handler.BadRequest(RESTAPI::Errors::VenueMissingBoardId);
			return false;
		case ValidateMacResult::TopologyNotFound:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		case ValidateMacResult::MacNotPresentInTopology:
			handler.BadRequest(RESTAPI::Errors::MacNotPresentInTopology);
			return false;
		case ValidateMacResult::TopologyUnusable:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		handler.InternalError(RESTAPI::Errors::InternalError);
		return false;
	}

} // namespace OpenWifi::RESTAPI::ParentalControl
