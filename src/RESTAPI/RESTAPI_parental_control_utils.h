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
#include <map>
#include <optional>
#include <string>

namespace OpenWifi::RESTAPI::ParentalControl {

	// =========================================================================
	// Blocked-Client Evaluation (used by topology response)
	// =========================================================================

	bool GetBlockedClients(const Poco::JSON::Object::Ptr &config, std::list<std::string> &blockedMacs);
	bool GetBlockedClients(const Poco::JSON::Object::Ptr &config, std::map<std::string, std::string> &blockedMacsWithUntil, const std::string &timezoneStr = "");

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


	// Forwards a parental-control error response through UserPortal. Normalizes the error body if it matches
	// the parental-control error shape; otherwise falls back to standard ForwardErrorResponse.
	void ForwardParentalControlErrorResponse(
		RESTAPIHandler *handler,
		Poco::Net::HTTPResponse::HTTPStatus status,
		const Poco::JSON::Object::Ptr &downstreamResponse);

	bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result);

	// Validates the two standard parental-control preconditions that appear at the top of
	// every mutating handler. Returns true when all required fields are present; otherwise
	// - If subscriberId is empty: sends UnAuthorized(InvalidSubscriberId).
	// - If requireOperatorId is true and operatorId is empty: sends UnAuthorized(OperatorIdMustExist).
	bool ValidateAuthPreconditions(RESTAPIHandler &handler, const std::string &subscriberId, const std::string &operatorId, bool requireOperatorId);

	// Extracts the config-raw snapshot from a parental-control SDK mutation response
	// and, when one is present, applies it to the subscriber's gateway.
	// - Calls ExtractConfigRawSnapshot on mutationResponse.
	// - If configRawRequired is false and no config-raw is present, returns true immediately (nothing to apply).
	// - If configRawRequired is true and config-raw is missing, logs an error, sends InternalError, and returns false.
	// - When config-raw is present, calls ApplyConfigRaw then HandleApplyConfigRawResult and returns its result.
	bool ApplyConfigRawFromMutationResponse(RESTAPIHandler &handler,
	                                        Poco::Logger &logger,
	                                        const std::string &subscriberId,
	                                        const std::string &operatorId,
	                                        const std::string &applyTargetId,
	                                        const Poco::JSON::Object::Ptr &mutationResponse,
	                                        const std::string &operationName,
	                                        const std::string &objectType,
	                                        bool configRawRequired = false,
	                                        const std::string &invalidPayloadContext = "");

	// Removes internal config-raw field from mutation responses before returning them to clients.
	void StripConfigRawFromMutationResponse(Poco::JSON::Object::Ptr mutationResponse);

	enum class MutationSuccessResponse {
		Ok,
		ReturnObject,
		ReturnObjectWithoutConfigRaw,
		ReturnNormalizedScheduleObject
	};

	struct MutationCallResult {
		bool success = false;
		Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_OK;
		Poco::JSON::Object::Ptr response;
	};

	// Common orchestration helper for handling SDK mutation call results.
	void HandleParentalControlMutationResult(RESTAPIHandler &handler,
	                                         Poco::Logger &logger,
	                                         const MutationCallResult &mutation,
	                                         const std::string &subscriberId,
	                                         const std::string &operatorId,
	                                         const std::string &applyTargetId,
	                                         const std::string &operationName,
	                                         const std::string &objectType,
	                                         bool configRawRequired,
	                                         const std::string &invalidPayloadContext,
	                                         MutationSuccessResponse successResponse,
	                                         const std::string &normalizeTimezone = "");

} // namespace OpenWifi::RESTAPI::ParentalControl
