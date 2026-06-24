/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include "framework/RESTAPI_Handler.h"
#include <optional>
#include <string>

namespace OpenWifi::RESTAPI::ParentalControl {

	enum class ApplyConfigRawResult {
		NoConfigApplyNeeded,
		Applied,
		MissingOperatorId,
		ProvisioningLookupFailed,
		MissingGatewaySerial,
		GatewayConfigLoadFailed,
		GatewayConfigMalformed,
		GatewayConfigureFailed
	};

	bool NormalizeScheduleResponse(Poco::JSON::Object::Ptr schedule);

	bool ParseTimeString(const Poco::Dynamic::Var &value, int &minuteOfDay);

	bool ValidateWeekdays(const Poco::JSON::Array::Ptr &weekdays);

	bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &callResponse,
								  Poco::JSON::Array::Ptr &configRaw,
								  bool required = false);

	ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &handler, Poco::Logger &logger,
										const std::string &subscriberId,
										const std::string &operatorId, const std::string &objectId,
										const Poco::JSON::Array::Ptr &configRaw,
										const std::string &operationName,
										const std::string &objectType,
										const std::string &gatewaySerial = "");

	struct ParsedScheduleRequest {
		std::string name;
		std::optional<std::string> description;
		bool enabled = true;
		std::string targetKind;
		std::string targetValue;
		int startMinute = 0;
		int stopMinute = 0;
		Poco::JSON::Array::Ptr weekdays;
	};

	bool ParseAndValidateScheduleRequest(RESTAPIHandler &handler,
										  const Poco::JSON::Object::Ptr &body,
										  bool enabledRequired,
										  ParsedScheduleRequest &out);

	Poco::JSON::Object BuildScheduleRequestBody(const ParsedScheduleRequest &req);

	bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result);

	enum class ValidateMacResult {
		Success,
		MissingSubscriberOrOperator,
		SubscriberDevicesNotFound,
		ProvisioningLookupFailed,
		GatewaySerialNotFound,
		InventoryNotFound,
		VenueNotFound,
		BoardIdNotFound,
		TopologyNotFound,
		MacNotPresentInTopology,
		TopologyUnusable
	};

	ValidateMacResult ValidateMacInTopology(RESTAPIHandler &handler,
											const std::string &subscriberId,
											const std::string &operatorId,
											const std::string &clientMac,
											std::string &gatewaySerial);

	bool HandleValidateMacResult(RESTAPIHandler &handler, ValidateMacResult result);

} // namespace OpenWifi::RESTAPI::ParentalControl
