/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_schedules_handler.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_schedules_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		const auto scheduleId = GetBinding("schedule_id", "");
		if (scheduleId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(scheduleId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		std::string timezone;
		if (!RESTAPI::ParentalControl::ResolveSubscriberTimezone(*this, UserInfo_.userinfo.id, timezone)) {
			return; // Response already sent inside resolver
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (!SDK::ParentalControl::GetSchedule(this, UserInfo_.userinfo.id, scheduleId, callStatus,
											   callResponse)) {
			return RESTAPI::ParentalControl::ForwardParentalControlErrorResponse(this, callStatus, callResponse);
		}

		if (!RESTAPI::ParentalControl::NormalizeScheduleResponse(callResponse, timezone)) {
			return InternalError(RESTAPI::Errors::InternalError);
		}
		return ReturnObject(*callResponse);
	}

	void RESTAPI_schedules_handler::DoPut() {
		if (!RESTAPI::ParentalControl::ValidateAuthPreconditions(*this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner, false)) {
			return;
		}

		const auto scheduleId = GetBinding("schedule_id", "");
		if (scheduleId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(scheduleId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		RESTAPI::ParentalControl::ParsedScheduleRequest req;
		if (!RESTAPI::ParentalControl::ParseAndValidateScheduleRequest(*this, ParsedBody_,
																	   /*enabledRequired=*/true,
																	   req)) {
			return;
		}

		std::string timezone;
		if (!RESTAPI::ParentalControl::ResolveSubscriberTimezone(*this, UserInfo_.userinfo.id, timezone)) {
			return; // Response already sent inside resolver
		}

		if (!RESTAPI::ParentalControl::ConvertScheduleTimesToUtc(timezone, req)) {
			return InternalError(RESTAPI::Errors::InternalError);
		}

		Poco::JSON::Object body = RESTAPI::ParentalControl::BuildScheduleRequestBody(req);

		RESTAPI::ParentalControl::MutationCallResult mutation;
		mutation.success = SDK::ParentalControl::UpdateSchedule(this, UserInfo_.userinfo.id, scheduleId, body, mutation.status, mutation.response);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
		    *this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
		    scheduleId, "DoPut", "schedule", /*configRawRequired=*/false,
		    fmt::format("subscriber={} schedule={}", UserInfo_.userinfo.id, scheduleId),
		    RESTAPI::ParentalControl::MutationSuccessResponse::ReturnNormalizedScheduleObject,
		    timezone);
	}

	void RESTAPI_schedules_handler::DoDelete() {
		if (!RESTAPI::ParentalControl::ValidateAuthPreconditions(*this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner, false)) {
			return;
		}

		const auto scheduleId = GetBinding("schedule_id", "");
		if (scheduleId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(scheduleId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		RESTAPI::ParentalControl::MutationCallResult mutation;
		std::string rawResponseBody;
		mutation.success = SDK::ParentalControl::DeleteSchedule(this, UserInfo_.userinfo.id, scheduleId, mutation.status, mutation.response, rawResponseBody);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
		    *this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
		    scheduleId, "DoDelete", "schedule", /*configRawRequired=*/false,
		    fmt::format("subscriber={} schedule={}", UserInfo_.userinfo.id, scheduleId),
		    RESTAPI::ParentalControl::MutationSuccessResponse::Ok);
	}

} // namespace OpenWifi
