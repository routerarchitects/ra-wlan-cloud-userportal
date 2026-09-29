/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_group_devices_list_v2_handler.h"
#include "Poco/JSON/Stringifier.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"

namespace OpenWifi {

	void RESTAPI_group_devices_list_v2_handler::DoPost() {
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
			if (name != "client_macs") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("client_macs") || ParsedBody_->isNull("client_macs") || !ParsedBody_->isArray("client_macs")) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_macs is required and must be an array");
		}

		auto macsArray = ParsedBody_->getArray("client_macs");
		if (!macsArray || macsArray->size() == 0) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_macs must contain at least one MAC address");
		}
		if (macsArray->size() > 100) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "client_macs must not exceed 100 MAC addresses");
		}

		auto downstreamMacsArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
		for (std::size_t i = 0; i < macsArray->size(); ++i) {
			if (macsArray->isNull(i) || !macsArray->get(i).isString()) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Each element in client_macs must be a string");
			}
			std::string rawMac = macsArray->getElement<std::string>(i);
			if (!Utils::NormalizeMac(rawMac)) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Invalid MAC address: " + rawMac);
			}
			downstreamMacsArray->add(Utils::SerialToMAC(rawMac));
		}

		RESTAPI::ParentalControl::MutationCallResult mutation;
		Poco::JSON::Object downstreamBody;
		downstreamBody.set("client_macs", downstreamMacsArray);
		mutation.success = SDK::ParentalControl::CreateGroupDevicesV2(
			this, UserInfo_.userinfo.id, groupId, downstreamBody, mutation.status, mutation.response);

		return RESTAPI::ParentalControl::HandleParentalControlMutationResult(
			*this, Logger(), mutation, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
			groupId, "DoPost", "group_devices_v2", /*configRawRequired=*/true,
			fmt::format("subscriber={} group={}", UserInfo_.userinfo.id, groupId),
			RESTAPI::ParentalControl::MutationSuccessResponse::ReturnObjectWithoutConfigRaw);
	}

} // namespace OpenWifi
