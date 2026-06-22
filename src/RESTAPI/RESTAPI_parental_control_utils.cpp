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

		if (!body->has("target_value")) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "target_value is required");
			return false;
		}
		if (body->isNull("target_value")) {
			if (out.targetKind != "INTERNET") {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "APP schedules require a non-empty target_value");
				return false;
			}
		} else {
			if (!body->get("target_value").isString()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "target_value must be a string or null");
				return false;
			}
			out.targetValue = body->getValue<std::string>("target_value");
			Poco::trimInPlace(out.targetValue);
			if (out.targetKind == "APP") {
				if (out.targetValue.empty()) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "APP schedules require a non-empty target_value");
					return false;
				}
			} else {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "INTERNET schedules require target_value to be null");
				return false;
			}
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

		if (enabledRequired && !body->has("description")) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "description is required for PUT");
			return false;
		}

		if (body->isNull("description")) {
			out.description = std::nullopt;
		} else if (body->has("description")) {
			if (!body->get("description").isString()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "description must be a string or null");
				return false;
			}
			std::string desc = body->getValue<std::string>("description");
			Poco::trimInPlace(desc);
			out.description = std::move(desc);
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
								  Poco::JSON::Array::Ptr &configRaw) {
		configRaw.reset();
		if (!callResponse) {
			return true;
		}
		if (!callResponse->has("config-raw") || callResponse->isNull("config-raw")) {
			return true;
		}
		if (!callResponse->isArray("config-raw")) {
			return false;
		}
		configRaw = callResponse->getArray("config-raw");
		if (!configRaw) {
			return false;
		}
		return true;
	}

	ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &handler, Poco::Logger &logger,
										const std::string &subscriberId,
										const std::string &operatorId, const std::string &objectId,
										const Poco::JSON::Array::Ptr &configRaw,
										const std::string &operationName,
										const std::string &objectType) {
		if (!configRaw) {
			return ApplyConfigRawResult::NoConfigApplyNeeded;
		}

		if (operatorId.empty()) {
			logger.error(fmt::format("{}: operator id missing for gateway apply (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingOperatorId;
		}

		ProvObjects::SubscriberDeviceList devList;
		Poco::Net::HTTPResponse::HTTPStatus provStatus;
		Poco::JSON::Object::Ptr provResponse;
		if (!SDK::Prov::Subscriber::GetDevices(&handler, subscriberId, operatorId, devList,
											   provStatus, provResponse)) {
			logger.error(fmt::format("{}: provisioning lookup failed (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::ProvisioningLookupFailed;
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
			logger.error(fmt::format("{}: gateway serial not resolved (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingGatewaySerial;
		}

		Poco::JSON::Object::Ptr gwResponse;
		Poco::Net::HTTPResponse::HTTPStatus gwStatus;
		if (!SDK::GW::Device::GetConfig(&handler, gatewaySerial, gwStatus, gwResponse)) {
			if (gwStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
										 gatewaySerial));
			} else {
				logger.error(fmt::format("{}: gateway config load failed (serial={})",
										 operationName, gatewaySerial));
			}
			return ApplyConfigRawResult::GatewayConfigLoadFailed;
		}

		if (!gwResponse || !gwResponse->has("configuration") ||
			!gwResponse->isObject("configuration")) {
			logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
									 gatewaySerial));
			return ApplyConfigRawResult::GatewayConfigMalformed;
		}

		auto gatewayConfig = gwResponse->getObject("configuration");
		gatewayConfig->set("config-raw", configRaw);

		Poco::JSON::Object::Ptr configureResponse;
		Poco::Net::HTTPResponse::HTTPStatus configureStatus;
		if (!SDK::GW::Device::Configure(&handler, gatewaySerial, gatewayConfig, configureStatus,
										configureResponse)) {
			logger.error(fmt::format("{}: gateway configure failed (serial={})", operationName,
									 gatewaySerial));
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
			handler.BadRequest(RESTAPI::Errors::OperatorIdMustExist);
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

} // namespace OpenWifi::RESTAPI::ParentalControl
