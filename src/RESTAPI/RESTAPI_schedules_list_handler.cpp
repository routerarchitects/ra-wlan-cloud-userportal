/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_schedules_list_handler.h"
#include "Poco/JSON/Stringifier.h"
#include "RESTAPI_parental_control_utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_schedules_list_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Array::Ptr arrayResponse;
		Poco::JSON::Object::Ptr errorResponse;

		if (!SDK::ParentalControl::GetSchedules(this, UserInfo_.userinfo.id, callStatus,
												arrayResponse, errorResponse)) {
			return ForwardErrorResponse(this, callStatus, errorResponse);
		}

		for (std::size_t i = 0; i < arrayResponse->size(); ++i) {
			if (!arrayResponse->isObject(i) ||
				!RESTAPI::ParentalControl::NormalizeScheduleResponse(arrayResponse->getObject(i))) {
				return InternalError(RESTAPI::Errors::InternalError);
			}
		}

		std::ostringstream ss;
		Poco::JSON::Stringifier::condense(*arrayResponse, ss);
		return ReturnRawJSON(ss.str());
	}

	void RESTAPI_schedules_list_handler::DoPost() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		RESTAPI::ParentalControl::ParsedScheduleRequest req;
		if (!RESTAPI::ParentalControl::ParseAndValidateScheduleRequest(*this, ParsedBody_,
																	   /*enabledRequired=*/false,
																	   req)) {
			return;
		}

		Poco::JSON::Object body = RESTAPI::ParentalControl::BuildScheduleRequestBody(req);

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (!SDK::ParentalControl::CreateSchedule(this, UserInfo_.userinfo.id, body, callStatus,
												  callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		if (!RESTAPI::ParentalControl::NormalizeScheduleResponse(callResponse)) {
			return InternalError(RESTAPI::Errors::InternalError);
		}
		return ReturnObject(*callResponse);
	}

} // namespace OpenWifi
