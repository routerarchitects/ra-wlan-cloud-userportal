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
		return ForwardErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoPut() {
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
		} else {
			body.set("description", Poco::Dynamic::Var());
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (SDK::ParentalControl::UpdateGroup(this, UserInfo_.userinfo.id, groupId, body,
											  callStatus, callResponse)) {
			return ReturnObject(*callResponse);
		}
		return ForwardErrorResponse(this, callStatus, callResponse);
	}

	void RESTAPI_groups_handler::DoDelete() {
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
		std::string rawResponseBody;

		if (!SDK::ParentalControl::DeleteGroup(this, UserInfo_.userinfo.id, groupId, callStatus,
											   callResponse, rawResponseBody)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr deleteConfigRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, deleteConfigRaw)) {
			Logger().error(fmt::format("DoDelete: invalid parental-control delete payload "
									   "(subscriber={} group={})",
									   UserInfo_.userinfo.id, groupId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, groupId,
																deleteConfigRaw, "DoDelete", "group"))) {
			return;
		}

		// YAML contract: DELETE public success returns empty HTTP 200.
		return OK();
	}

} // namespace OpenWifi
