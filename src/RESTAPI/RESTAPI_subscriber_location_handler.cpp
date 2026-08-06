/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_subscriber_location_handler.h"
#include "Poco/String.h"
#include "sdks/SDK_prov.h"
#include <date/tz.h>

namespace OpenWifi {

	void RESTAPI_subscriber_location_handler::DoPost() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		if (!ParsedBody_->has("timezone") ||
		    ParsedBody_->isNull("timezone") ||
		    !ParsedBody_->get("timezone").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		std::string timezone = ParsedBody_->getValue<std::string>("timezone");
		Poco::trimInPlace(timezone);

		if (timezone.empty()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		try {
			date::locate_zone(timezone);
		} catch (...) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		ParsedBody_->set("timezone", timezone);

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::VenueList venueList;

		if (!SDK::Prov::Venue::GetVenues(nullptr, UserInfo_.userinfo.id, venueList, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				return ForwardErrorResponse(this, callStatus, callResponse);
			}
			return InternalError(RESTAPI::Errors::InvalidJSONDocument);
		}

		if (venueList.venues.empty()) {
			return NotFound();
		}

		const auto &venue = venueList.venues[0];
		const std::string venueId = venue.info.id;
		if (venueId.empty()) {
			return InternalError(RESTAPI::Errors::InvalidJSONDocument);
		}

		if (!venue.location.empty()) {
			return BadRequest(RESTAPI::Errors::SubscriberLocationAlreadyConfigured);
		}

		if (!SDK::Prov::Venue::CreateLocation(nullptr, venueId, ParsedBody_, callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}
		return OK();
	}

	void RESTAPI_subscriber_location_handler::DoDelete() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::VenueList venueList;

		if (!SDK::Prov::Venue::GetVenues(nullptr, UserInfo_.userinfo.id, venueList, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				return ForwardErrorResponse(this, callStatus, callResponse);
			}
			return InternalError(RESTAPI::Errors::InvalidJSONDocument);
		}

		if (venueList.venues.empty()) {
			return NotFound();
		}

		const auto &venue = venueList.venues[0];
		const std::string venueId = venue.info.id;
		if (venueId.empty()) {
			return InternalError(RESTAPI::Errors::InvalidJSONDocument);
		}

		if (venue.location.empty()) {
			return NotFound();
		}

		const std::string locationId = venue.location;

		if (!SDK::Prov::Venue::ClearLocation(nullptr, venueId, callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		callResponse = Poco::makeShared<Poco::JSON::Object>();

		if (!SDK::Prov::Location::Delete(nullptr, locationId, callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		return OK();
	}

} // namespace OpenWifi
