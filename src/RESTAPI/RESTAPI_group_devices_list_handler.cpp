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
			return ForwardErrorResponse(this, callStatus, objectResponse);
		}

		if (!arrayResponse) {
			return InternalError(RESTAPI::Errors::InternalError);
		}

		std::ostringstream ss;
		Poco::JSON::Stringifier::condense(*arrayResponse, ss);
		return ReturnRawJSON(ss.str());
	}

	void RESTAPI_group_devices_list_handler::DoPost() {
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

		// topology validation check
		std::string gatewaySerial;
		RESTAPI::ParentalControl::ValidateMacResult valRes =
			RESTAPI::ParentalControl::ValidateMacInTopology(*this, UserInfo_.userinfo.id,
															UserInfo_.userinfo.owner,
															normalizedMac, gatewaySerial);
		if (!RESTAPI::ParentalControl::HandleValidateMacResult(*this, valRes)) {
			return;
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		Poco::JSON::Object downstreamBody;
		downstreamBody.set("client_mac", normalizedMac);
		if (!SDK::ParentalControl::CreateGroupDevice(this, UserInfo_.userinfo.id, groupId, downstreamBody,
													 callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw, true)) {
			Logger().error(fmt::format("DoPost: invalid parental-control payload (subscriber={} group={})",
									   UserInfo_.userinfo.id, groupId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, groupId,
																configRaw, "DoPost", "group_device",
																gatewaySerial))) {
			return;
		}

		if (callResponse->has("config-raw")) {
			callResponse->remove("config-raw");
		}

		return ReturnObject(*callResponse);
	}

} // namespace OpenWifi
