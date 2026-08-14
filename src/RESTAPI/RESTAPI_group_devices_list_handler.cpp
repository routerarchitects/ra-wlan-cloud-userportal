/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_group_devices_list_handler.h"
#include "Poco/JSON/Stringifier.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_group_devices_list_handler::DoGet() {
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
		Poco::JSON::Array::Ptr arrayResponse;
		Poco::JSON::Object::Ptr objectResponse;

		if (!SDK::ParentalControl::GetGroupDevices(this, UserInfo_.userinfo.id, groupId, callStatus,
												  arrayResponse, objectResponse)) {
			return RESTAPI::ParentalControl::ForwardParentalControlErrorResponse(this, callStatus, objectResponse);
		}

		if (!arrayResponse) {
			return InternalError(RESTAPI::Errors::InternalError);
		}

		std::ostringstream ss;
		Poco::JSON::Stringifier::condense(*arrayResponse, ss);
		return ReturnRawJSON(ss.str());
	}

	void RESTAPI_group_devices_list_handler::DoPost() {
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

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		std::vector<std::string> names;
		ParsedBody_->getNames(names);
		for (const auto &name : names) {
			if (name != "client_mac") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("client_mac") || ParsedBody_->isNull("client_mac") || !ParsedBody_->get("client_mac").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_mac is required and must be a string");
		}
		std::string clientMac = ParsedBody_->getValue<std::string>("client_mac");
		if (!Utils::NormalizeMac(clientMac)) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_mac is not a valid MAC address");
		}
		std::string normalizedMac = Utils::SerialToMAC(clientMac);

		RESTAPI::ParentalControl::MutationCallResult mutation;
		Poco::JSON::Object downstreamBody;
		downstreamBody.set("client_mac", normalizedMac);
		mutation.success = SDK::ParentalControl::CreateGroupDevice(this, UserInfo_.userinfo.id, groupId, downstreamBody, mutation.status, mutation.response);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
		    *this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
		    groupId, "DoPost", "group_device", /*configRawRequired=*/true,
		    fmt::format("subscriber={} group={}", UserInfo_.userinfo.id, groupId),
		    RESTAPI::ParentalControl::MutationSuccessResponse::ReturnObjectWithoutConfigRaw);
	}

} // namespace OpenWifi
