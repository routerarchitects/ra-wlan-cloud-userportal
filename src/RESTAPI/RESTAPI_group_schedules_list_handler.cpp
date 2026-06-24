/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_group_schedules_list_handler.h"
#include "Poco/JSON/Stringifier.h"
#include "RESTAPI_parental_control_utils.h"
#include "fmt/format.h"
#include "framework/utils.h"
#include "sdks/SDK_parental_control.h"
#include <set>

namespace OpenWifi {

	void RESTAPI_group_schedules_list_handler::DoGet() {
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

		if (!SDK::ParentalControl::GetGroupSchedules(this, UserInfo_.userinfo.id, groupId, callStatus,
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

	void RESTAPI_group_schedules_list_handler::DoPost() {
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
			if (name != "schedule_id") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("schedule_id") || ParsedBody_->isNull("schedule_id") || !ParsedBody_->get("schedule_id").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "schedule_id is required and must be a string");
		}
		std::string scheduleId = ParsedBody_->getValue<std::string>("schedule_id");
		if (!Utils::ValidUUID(scheduleId)) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "schedule_id is not a valid UUID");
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		if (!SDK::ParentalControl::CreateGroupSchedule(this, UserInfo_.userinfo.id, groupId, *ParsedBody_,
													   callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw, true)) {
			Logger().error(fmt::format("DoPost: invalid parental-control payload (subscriber={} group={} schedule={})",
									   UserInfo_.userinfo.id, groupId, scheduleId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, groupId,
																configRaw, "DoPost", "group_schedule"))) {
			return;
		}

		if (callResponse->has("config-raw")) {
			callResponse->remove("config-raw");
		}

		return ReturnObject(*callResponse);
	}

	void RESTAPI_group_schedules_list_handler::DoPut() {
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
			if (name != "schedule_ids") {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "Unknown field: " + name);
			}
		}

		if (!ParsedBody_->has("schedule_ids") || ParsedBody_->isNull("schedule_ids") || !ParsedBody_->isArray("schedule_ids")) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "schedule_ids is required and must be an array");
		}

		auto scheduleIdsArray = ParsedBody_->getArray("schedule_ids");
		if (!scheduleIdsArray) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "schedule_ids is invalid");
		}

		std::set<std::string> uniqueIds;
		for (std::size_t i = 0; i < scheduleIdsArray->size(); ++i) {
			try {
				if (!scheduleIdsArray->get(i).isString()) {
					return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "schedule_ids entries must be strings");
				}
				std::string id = scheduleIdsArray->getElement<std::string>(i);
				if (!Utils::ValidUUID(id)) {
					return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "invalid UUID in schedule_ids");
				}
				if (!uniqueIds.insert(id).second) {
					return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "duplicate UUID in schedule_ids");
				}
			} catch (...) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "invalid UUID in schedule_ids");
			}
		}

		Poco::Net::HTTPResponse::HTTPStatus callStatus;
		Poco::JSON::Object::Ptr callResponse;
		if (!SDK::ParentalControl::ReplaceGroupSchedules(this, UserInfo_.userinfo.id, groupId, *ParsedBody_,
														 callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		Poco::JSON::Array::Ptr configRaw;
		if (!RESTAPI::ParentalControl::ExtractConfigRawSnapshot(callResponse, configRaw, true)) {
			Logger().error(fmt::format("DoPut: invalid parental-control payload (subscriber={} group={})",
									   UserInfo_.userinfo.id, groupId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (!RESTAPI::ParentalControl::HandleApplyConfigRawResult(
				*this, RESTAPI::ParentalControl::ApplyConfigRaw(*this, Logger(),
																UserInfo_.userinfo.id,
																UserInfo_.userinfo.owner, groupId,
																configRaw, "DoPut", "group_schedule"))) {
			return;
		}

		if (callResponse->has("config-raw")) {
			callResponse->remove("config-raw");
		}

		return ReturnObject(*callResponse);
	}

} // namespace OpenWifi
