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

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (!SDK::ParentalControl::GetSchedule(this, UserInfo_.userinfo.id, scheduleId, callStatus,
											   callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		if (!RESTAPI::ParentalControl::NormalizeScheduleResponse(callResponse)) {
			return InternalError(RESTAPI::Errors::InternalError);
		}
		return ReturnObject(*callResponse);
	}

	void RESTAPI_schedules_handler::DoPut() {
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

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		RESTAPI::ParentalControl::ParsedScheduleRequest req;
		if (!RESTAPI::ParentalControl::ParseAndValidateScheduleRequest(*this, ParsedBody_,
																	   /*enabledRequired=*/true,
																	   req)) {
			return;
		}

		Poco::JSON::Object body = RESTAPI::ParentalControl::BuildScheduleRequestBody(req);

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		if (!SDK::ParentalControl::UpdateSchedule(this, UserInfo_.userinfo.id, scheduleId, body,
												  callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw)) {
			Logger().error(fmt::format("DoPut: invalid parental-control update payload "
									   "(subscriber={} schedule={})",
									   UserInfo_.userinfo.id, scheduleId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, scheduleId,
																configRaw, "DoPut", "schedule"))) {
			return;
		}

		if (!RESTAPI::ParentalControl::NormalizeScheduleResponse(callResponse)) {
			return InternalError(RESTAPI::Errors::InternalError);
		}
		return ReturnObject(*callResponse);
	}

	void RESTAPI_schedules_handler::DoDelete() {
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

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		std::string rawResponseBody;

		if (!SDK::ParentalControl::DeleteSchedule(this, UserInfo_.userinfo.id, scheduleId,
												  callStatus, callResponse, rawResponseBody)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw)) {
			Logger().error(fmt::format("DoDelete: invalid parental-control delete payload "
									   "(subscriber={} schedule={})",
									   UserInfo_.userinfo.id, scheduleId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, scheduleId,
																configRaw, "DoDelete", "schedule"))) {
			return;
		}

		return OK();
	}

} // namespace OpenWifi
