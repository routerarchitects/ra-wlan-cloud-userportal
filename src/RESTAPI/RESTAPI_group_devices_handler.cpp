/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_group_devices_handler.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_group_devices_handler::DoGet() {
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

		auto clientMac = GetBinding("client_mac", "");
		if (clientMac.empty() || !Utils::NormalizeMac(clientMac)) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_mac is missing or invalid");
		}
		std::string normalizedMac = Utils::SerialToMAC(clientMac);

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;

		if (!SDK::ParentalControl::GetGroupDevice(this, UserInfo_.userinfo.id, groupId, normalizedMac,
												 callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		return ReturnObject(*callResponse);
	}

	void RESTAPI_group_devices_handler::DoDelete() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}
		if (UserInfo_.userinfo.owner.empty()) {
			return UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
		}

		const auto groupId = GetBinding("group_id", "");
		if (groupId.empty()) {
			return BadRequest(RESTAPI::Errors::MissingUUID);
		}
		if (!Utils::ValidUUID(groupId)) {
			return BadRequest(RESTAPI::Errors::UnknownId);
		}

		auto clientMac = GetBinding("client_mac", "");
		if (clientMac.empty() || !Utils::NormalizeMac(clientMac)) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_mac is missing or invalid");
		}
		std::string normalizedMac = Utils::SerialToMAC(clientMac);

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		std::string rawResponseBody;

		if (!SDK::ParentalControl::DeleteGroupDevice(this, UserInfo_.userinfo.id, groupId, normalizedMac,
													 callStatus, callResponse, rawResponseBody)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw, true)) {
			Logger().error(fmt::format("DoDelete: invalid parental-control payload (subscriber={} group={} device={})",
									   UserInfo_.userinfo.id, groupId, normalizedMac));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, groupId,
																configRaw, "DoDelete", "group_device"))) {
			return;
		}

		return OK();
	}

} // namespace OpenWifi
