/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_groups_list_handler.h"
#include "sdks/SDK_parental_control.h"
#include "Poco/JSON/Stringifier.h"

namespace OpenWifi {

	void RESTAPI_groups_list_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Array::Ptr arrayResponse;
		Poco::JSON::Object::Ptr errorResponse;

		if (SDK::ParentalControl::GetGroups(this, UserInfo_.userinfo.id, callStatus, arrayResponse,
		                                    errorResponse)) {
			std::ostringstream ss;
			Poco::JSON::Stringifier::condense(*arrayResponse, ss);
			return ReturnRawJSON(ss.str());
		}
		return ForwardErrorResponse(this, callStatus, errorResponse);
	}

	void RESTAPI_groups_list_handler::DoPost() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		// Validate additionalProperties: false per GroupCreateRequest schema.
		std::vector<std::string> names;
		ParsedBody_->getNames(names);
		for (const auto &name : names) {
			if (name != "name" && name != "description") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
				                  "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("name") || ParsedBody_->isNull("name") ||
		    !ParsedBody_->get("name").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "name is required");
		}
		std::string nameVal = ParsedBody_->getValue<std::string>("name");
		Poco::trimInPlace(nameVal);
		if (nameVal.empty()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
			                  "name must be non-empty");
		}

		Poco::JSON::Object body;
		body.set("name", nameVal);
		if (ParsedBody_->has("description")) {
			if (ParsedBody_->isNull("description")) {
				body.set("description", Poco::Dynamic::Var());
			} else {
				if (!ParsedBody_->get("description").isString()) {
					return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
					                  "description must be a string or null");
				}
				std::string descVal = ParsedBody_->getValue<std::string>("description");
				Poco::trimInPlace(descVal);
				body.set("description", descVal);
			}
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (SDK::ParentalControl::CreateGroup(this, UserInfo_.userinfo.id, body, callStatus,
		                                      callResponse)) {
			return ReturnObject(*callResponse);
		}
		return ForwardErrorResponse(this, callStatus, callResponse);
	}

} // namespace OpenWifi
