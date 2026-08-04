/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include "framework/RESTAPI_Handler.h"
#include <list>
#include <optional>
#include <string>

namespace OpenWifi::RESTAPI::ParentalControl {

	// =========================================================================
	// Blocked-Client Evaluation (used by topology response)
	// =========================================================================

	bool GetBlockedClients(const Poco::JSON::Object::Ptr &config, std::list<std::string> &blockedMacs);

	// =========================================================================
	// Schedule Helpers
	// =========================================================================

	struct ParsedScheduleRequest {
		std::string name;
		std::optional<std::string> description;
		bool has_description = false;
		bool enabled = true;
		std::string targetKind;
		std::string targetValue;
		bool has_target_value = false;
		int startMinute = 0;
		int stopMinute = 0;
		Poco::JSON::Array::Ptr weekdays;
	};

	// Normalize raw parental-control response object by converting UTC start_minute
	// and stop_minute back to subscriber-local start_time and stop_time (HH:MM),
	// shifting weekdays to match the local start day, and stripping internal fields.
	bool NormalizeScheduleResponse(Poco::JSON::Object::Ptr schedule, const std::string &timezone);

	// Parse an "HH:MM" string into minute of day (0..1439). Returns false if malformed.
	bool ParseTimeString(const Poco::Dynamic::Var &value, int &minuteOfDay);

	// Validate that the weekdays array is non-empty and contains distinct values in 0..6.
	bool ValidateWeekdays(const Poco::JSON::Array::Ptr &weekdays);

	// Parse and validate schedule POST/PUT JSON request body against schema rules.
	// On validation failure, sets HTTP 400 Bad Request error on handler and returns false.
	bool ParseAndValidateScheduleRequest(RESTAPIHandler &handler,
										  const Poco::JSON::Object::Ptr &body,
										  bool enabledRequired,
										  ParsedScheduleRequest &out);

	// Build the backend JSON request payload for mango-parental-control from a parsed schedule.
	Poco::JSON::Object BuildScheduleRequestBody(const ParsedScheduleRequest &req);

	// Resolve the IANA timezone string for the given subscriber by fetching
	// their venue and location from OWProv. Returns true and sets timezone on
	// success. On failure, sets the appropriate HTTP error on handler and returns false.
	bool ResolveSubscriberTimezone(RESTAPIHandler &handler, const std::string &subscriberId, std::string &timezone);

	// Convert request.startMinute and request.stopMinute from the local timezone
	// to UTC in-place. Handles overnight schedules (stopMinute <= startMinute)
	// and shifts weekdays when the UTC calendar day differs from the local day.
	// Returns false on invalid timezone or conversion failure; does NOT set any
	// HTTP error (caller decides the response).
	bool ConvertScheduleTimesToUtc(const std::string &timezoneStr, ParsedScheduleRequest &request);



	// =========================================================================
	// Topology/Device Validation Helpers
	// =========================================================================

	enum class ValidateMacResult {
		Success,
		MissingSubscriberOrOperator,
		SubscriberDevicesNotFound,
		ProvisioningLookupFailed,
		GatewaySerialNotFound,
		InventoryNotFound,
		VenueNotFound,
		VenueLookupFailed,
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


	// =========================================================================
	// Config-Raw Extraction/Apply Helpers
	// =========================================================================

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


	// =========================================================================
	// HTTP/Result Mapping Helpers
	// =========================================================================

	bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result);

	bool HandleValidateMacResult(RESTAPIHandler &handler, ValidateMacResult result);

} // namespace OpenWifi::RESTAPI::ParentalControl
