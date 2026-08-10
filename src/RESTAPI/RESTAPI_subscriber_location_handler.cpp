/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_subscriber_location_handler.h"
#include "Poco/String.h"
#include "sdks/SDK_prov.h"
#include <date/tz.h>
#include <set>

namespace OpenWifi {

	void RESTAPI_subscriber_location_handler::DoGet() {
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
		if (venue.location.empty()) {
			return NotFound();
		}

		ProvObjects::Location location;
		callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		callResponse = Poco::makeShared<Poco::JSON::Object>();

		if (!SDK::Prov::Location::Get(nullptr, venue.location, location, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				return ForwardErrorResponse(this, callStatus, callResponse);
			}
			return InternalError(RESTAPI::Errors::InvalidJSONDocument);
		}

		return ReturnObject(*callResponse);
	}

	namespace {
		static const std::set<std::string> kAllowedLocationKeys = {
			"name", "description", "type", "buildingName", "addressLines",
			"city", "state", "postal", "country", "phones", "mobiles",
			"geoCode", "timezone"
		};

		bool BuildAllowedLocationBody(
			const Poco::JSON::Object::Ptr &input,
			Poco::JSON::Object::Ptr &output) {

			if (!input) {
				return false;
			}

			output = Poco::makeShared<Poco::JSON::Object>();

			for (const auto &entry : *input) {
				if (kAllowedLocationKeys.find(entry.first) == kAllowedLocationKeys.end()) {
					return false;
				}
				output->set(entry.first, entry.second);
			}

			return true;
		}
	} // namespace

	void RESTAPI_subscriber_location_handler::DoPost() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		Poco::JSON::Object::Ptr locationBody;
		if (!BuildAllowedLocationBody(ParsedBody_, locationBody)) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		if (!locationBody->has("timezone") ||
		    locationBody->isNull("timezone") ||
		    !locationBody->get("timezone").isString()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		std::string timezone = locationBody->getValue<std::string>("timezone");
		Poco::trimInPlace(timezone);

		if (timezone.empty()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		try {
			date::locate_zone(timezone);
		} catch (...) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		locationBody->set("timezone", timezone);

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

		if (!SDK::Prov::Venue::CreateLocation(nullptr, venueId, locationBody, callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}
		return OK();
	}

	void RESTAPI_subscriber_location_handler::DoPut() {
		if (UserInfo_.userinfo.id.empty()) {
			return UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
		}

		if (!ParsedBody_) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		Poco::JSON::Object::Ptr locationBody;
		if (!BuildAllowedLocationBody(ParsedBody_, locationBody) || locationBody->size() == 0) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		if (locationBody->has("timezone")) {
			if (locationBody->isNull("timezone") ||
				!locationBody->get("timezone").isString()) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
			}

			std::string timezone =
				locationBody->getValue<std::string>("timezone");
			Poco::trimInPlace(timezone);

			if (timezone.empty()) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
			}

			try {
				date::locate_zone(timezone);
			} catch (...) {
				return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
			}

			locationBody->set("timezone", timezone);
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
		if (venue.location.empty()) {
			return NotFound();
		}

		const std::string locationId = venue.location;

		if (!SDK::Prov::Location::Put(nullptr, locationId, locationBody, callStatus, callResponse)) {
			return ForwardErrorResponse(this, callStatus, callResponse);
		}

		return ReturnObject(*callResponse);
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
