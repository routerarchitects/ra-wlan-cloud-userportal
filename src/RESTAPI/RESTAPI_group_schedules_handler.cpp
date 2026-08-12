/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_group_schedules_handler.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_group_schedules_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
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

		if (!SDK::ParentalControl::GetGroupSchedule(this, UserInfo_.userinfo.id, groupId, scheduleId,
													callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		return ReturnObject(*callResponse);
	}

	void RESTAPI_group_schedules_handler::DoDelete() {
		if (!RESTAPI::ParentalControl::ValidateAuthPreconditions(*this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner, true)) {
			return;
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
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
		mutation.success = SDK::ParentalControl::DeleteGroupSchedule(this, UserInfo_.userinfo.id, groupId, scheduleId, mutation.status, mutation.response, rawResponseBody);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
		    *this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
		    groupId, "DoDelete", "group_schedule", /*configRawRequired=*/true,
		    fmt::format("subscriber={} group={} schedule={}", UserInfo_.userinfo.id, groupId, scheduleId),
		    RESTAPI::ParentalControl::MutationSuccessResponse::Ok);
	}

} // namespace OpenWifi
