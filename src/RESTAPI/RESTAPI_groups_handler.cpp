/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_groups_handler.h"
#include "Poco/JSON/Array.h"
#include "Poco/JSON/Stringifier.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_groups_handler::DoGet() {
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

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (SDK::ParentalControl::GetGroup(this, UserInfo_.userinfo.id, groupId, callStatus,
										   callResponse)) {
			return ReturnObject(*callResponse);
		}
		return RESTAPI::ParentalControl::ForwardParentalControlErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoPut() {
		if (!RESTAPI::ParentalControl::ValidateAuthPreconditions(*this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner, false)) {
			return;
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		// GroupPutRequest: additionalProperties: false — reject unknown fields.
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

		if (SDK::ParentalControl::UpdateGroup(this, UserInfo_.userinfo.id, groupId, body,
											  callStatus, callResponse)) {
			return ReturnObject(*callResponse);
		}
		return RESTAPI::ParentalControl::ForwardParentalControlErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoDelete() {
		if (!RESTAPI::ParentalControl::ValidateAuthPreconditions(*this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner, false)) {
			return;
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		RESTAPI::ParentalControl::MutationCallResult mutation;
		std::string rawResponseBody;
		mutation.success = SDK::ParentalControl::DeleteGroup(this, UserInfo_.userinfo.id, groupId, mutation.status, mutation.response, rawResponseBody);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
		    *this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
		    groupId, "DoDelete", "group", /*configRawRequired=*/false,
		    fmt::format("subscriber={} group={}", UserInfo_.userinfo.id, groupId),
		    RESTAPI::ParentalControl::MutationSuccessResponse::Ok);
	}

} // namespace OpenWifi
