/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_schedule_groups_list_handler.h"
#include "RESTAPI_parental_control_utils.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"
#include "Poco/JSON/Stringifier.h"

namespace OpenWifi {

	void RESTAPI_schedule_groups_list_handler::DoGet() {
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
		Poco::JSON::Array::Ptr arrayResponse;
		Poco::JSON::Object::Ptr errorResponse;

		if (!SDK::ParentalControl::GetScheduleGroups(this, UserInfo_.userinfo.id, scheduleId,
		                                             callStatus, arrayResponse, errorResponse)) {
			return RESTAPI::ParentalControl::ForwardParentalControlErrorResponse(this, callStatus, errorResponse);
		}

		if (!arrayResponse) {
			return InternalError(RESTAPI::Errors::InternalError);
		}

		std::ostringstream ss;
		Poco::JSON::Stringifier::condense(*arrayResponse, ss);
		return ReturnRawJSON(ss.str());
	}

} // namespace OpenWifi
