/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_parental_control_utils.h"
#include "Poco/Format.h"
#include "Poco/DateTime.h"
#include "Poco/DateTimeFormatter.h"
#include "Poco/DateTimeParser.h"
#include "Poco/String.h"
#include "fmt/format.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_nw_topology.h"
#include "framework/utils.h"
#include "framework/ow_constants.h"
#include <date/tz.h>
#include <cctype>
#include <chrono>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace OpenWifi::RESTAPI::ParentalControl {

	namespace {
		bool MinuteToTimeString(int minuteOfDay, std::string &timeValue) {
			if (minuteOfDay < 0 || minuteOfDay > 1439) {
				return false;
			}

			const int hour = minuteOfDay / 60;
			const int minute = minuteOfDay % 60;
			timeValue = Poco::format("%02d:%02d", hour, minute);
			return true;
		}

		ValidateMacResult ResolveGatewaySerial(RESTAPIHandler &handler,
											   const std::string &subscriberId,
											   const std::string &operatorId,
											   std::string &gatewaySerial) {
			if (subscriberId.empty() || operatorId.empty()) {
				return ValidateMacResult::MissingSubscriberOrOperator;
			}

			ProvObjects::SubscriberDeviceList devList;
			Poco::Net::HTTPResponse::HTTPStatus provStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
			Poco::JSON::Object::Ptr provResponse;
			if (!SDK::Prov::Subscriber::GetDevices(&handler, subscriberId, operatorId, devList,
												   provStatus, provResponse)) {
				if (provStatus == Poco::Net::HTTPResponse::HTTP_NOT_FOUND) {
					return ValidateMacResult::SubscriberDevicesNotFound;
				}
				return ValidateMacResult::ProvisioningLookupFailed;
			}

			if (devList.subscriberDevices.empty()) {
				return ValidateMacResult::SubscriberDevicesNotFound;
			}

			for (const auto &dev : devList.subscriberDevices) {
				std::string grp = dev.deviceGroup;
				Poco::toLowerInPlace(grp);
				if (grp == "olg") {
					gatewaySerial = dev.serialNumber;
					break;
				}
			}

			if (gatewaySerial.empty()) {
				return ValidateMacResult::GatewaySerialNotFound;
			}

			return ValidateMacResult::Success;
		}

		ValidateMacResult ResolveVenueBoards(RESTAPIHandler &handler,
											 const std::string &gatewaySerial,
											 std::string &boardId) {
			ProvObjects::InventoryTag inventory;
			if (!SDK::Prov::Device::Get(&handler, gatewaySerial, inventory)) {
				return ValidateMacResult::InventoryNotFound;
			}

			if (inventory.venue.empty()) {
				return ValidateMacResult::VenueNotFound;
			}

			Poco::Net::HTTPServerResponse::HTTPStatus venueStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
			Poco::JSON::Object::Ptr venueResponse = Poco::makeShared<Poco::JSON::Object>();
			ProvObjects::Venue venue;
			if (!SDK::Prov::Venue::Get(&handler, inventory.venue, venue, venueStatus, venueResponse)) {
				if (venueStatus == Poco::Net::HTTPResponse::HTTP_NOT_FOUND) {
					return ValidateMacResult::VenueNotFound;
				}
				return ValidateMacResult::VenueLookupFailed;
			}

			if (venue.boards.empty() || venue.boards.front().empty()) {
				return ValidateMacResult::BoardIdNotFound;
			}

			boardId = venue.boards.front();
			return ValidateMacResult::Success;
		}

		ValidateMacResult FetchBoardTopology(RESTAPIHandler &handler,
											 const std::string &boardId,
											 Poco::JSON::Object::Ptr &topologyResponse) {
			Poco::Net::HTTPServerResponse::HTTPStatus topoStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
			topologyResponse = Poco::makeShared<Poco::JSON::Object>();
			if (!SDK::Topology::Get(nullptr, boardId, topoStatus, topologyResponse)) {
				return ValidateMacResult::TopologyNotFound;
			}

			if (!topologyResponse) {
				return ValidateMacResult::TopologyNotFound;
			}

			return ValidateMacResult::Success;
		}

		ValidateMacResult MacPresentInTopologyPayload(const Poco::JSON::Object::Ptr &topologyResponse,
													  const std::string &clientMac) {
			if (!topologyResponse) {
				return ValidateMacResult::TopologyNotFound;
			}

			// Check structures
			bool hasNodes = topologyResponse->has("nodes") && !topologyResponse->isNull("nodes");
			bool hasHistClients = topologyResponse->has("historicalClients") && !topologyResponse->isNull("historicalClients");
			bool hasHistDevs = topologyResponse->has("historicalDevices") && !topologyResponse->isNull("historicalDevices");

			if ((hasNodes && !topologyResponse->isArray("nodes")) ||
				(hasHistClients && !topologyResponse->isArray("historicalClients")) ||
				(hasHistDevs && !topologyResponse->isArray("historicalDevices")) ||
				(!hasNodes && !hasHistClients && !hasHistDevs)) {
				return ValidateMacResult::TopologyUnusable;
			}

			// Normalize client MAC to search
			std::string targetMac = clientMac;
			if (!Utils::NormalizeMac(targetMac)) {
				return ValidateMacResult::MacNotPresentInTopology;
			}
			std::string targetMacFormatted = Utils::SerialToMAC(targetMac);
			Poco::toLowerInPlace(targetMacFormatted);

			// Check historicalClients
			if (hasHistClients) {
				auto histClients = topologyResponse->getArray("historicalClients");
				for (std::size_t i = 0; i < histClients->size(); ++i) {
					auto item = histClients->getObject(i);
					if (!item) {
						return ValidateMacResult::TopologyUnusable;
					}
					if (item->has("station") && item->get("station").isString()) {
						std::string station = item->getValue<std::string>("station");
						Poco::toLowerInPlace(station);
						if (station == targetMacFormatted) {
							return ValidateMacResult::Success;
						}
					}
				}
			}

			// Check historicalDevices (array of MAC strings)
			if (hasHistDevs) {
				auto histDevs = topologyResponse->getArray("historicalDevices");
				for (std::size_t i = 0; i < histDevs->size(); ++i) {
					try {
						if (!histDevs->get(i).isString()) {
							return ValidateMacResult::TopologyUnusable;
						}
						std::string station = histDevs->getElement<std::string>(i);
						if (Utils::NormalizeMac(station)) {
							std::string normStation = Utils::SerialToMAC(station);
							Poco::toLowerInPlace(normStation);
							if (normStation == targetMacFormatted) {
								return ValidateMacResult::Success;
							}
						}
					} catch (...) {
						return ValidateMacResult::TopologyUnusable;
					}
				}
			}

			// Check active nodes / aps / clients
			if (hasNodes) {
				auto nodes = topologyResponse->getArray("nodes");
				for (std::size_t i = 0; i < nodes->size(); ++i) {
					auto node = nodes->getObject(i);
					if (!node) {
						return ValidateMacResult::TopologyUnusable;
					}
					if (node->has("aps") && !node->isNull("aps")) {
						if (!node->isArray("aps")) {
							return ValidateMacResult::TopologyUnusable;
						}
						auto aps = node->getArray("aps");
						for (std::size_t apIndex = 0; apIndex < aps->size(); ++apIndex) {
							auto ap = aps->getObject(apIndex);
							if (!ap) {
								return ValidateMacResult::TopologyUnusable;
							}
							if (ap->has("clients") && !ap->isNull("clients")) {
								if (!ap->isArray("clients")) {
									return ValidateMacResult::TopologyUnusable;
								}
								auto clients = ap->getArray("clients");
								for (std::size_t clientIndex = 0; clientIndex < clients->size(); ++clientIndex) {
									auto client = clients->getObject(clientIndex);
									if (!client) {
										return ValidateMacResult::TopologyUnusable;
									}
									if (client->has("station")) {
										if (!client->get("station").isString()) {
											return ValidateMacResult::TopologyUnusable;
										}
										std::string station = client->getValue<std::string>("station");
										Poco::toLowerInPlace(station);
										if (station == targetMacFormatted) {
											return ValidateMacResult::Success;
										}
									}
								}
							}
						}
					}
				}
			}

			return ValidateMacResult::MacNotPresentInTopology;
		}

		// Shift an array of weekdays (0=Sun..6=Sat) by a given day offset (positive or negative)
		// and wrap within [0, 6], deduplicating results.
		void ShiftWeekdays(Poco::JSON::Array::Ptr &weekdays, int shift) {
			if (!weekdays || weekdays->size() == 0 || shift == 0) {
				return;
			}
			std::set<int> shiftedDays;
			for (std::size_t i = 0; i < weekdays->size(); ++i) {
				int day = weekdays->getElement<int>(i);
				int newDay = ((day + shift) % 7 + 7) % 7;
				shiftedDays.insert(newDay);
			}
			auto newWeekdays = Poco::makeShared<Poco::JSON::Array>();
			for (int d : shiftedDays) {
				newWeekdays->add(d);
			}
			weekdays = newWeekdays;
		}
	} // namespace

	// =========================================================================
	// Schedule Helpers
	// =========================================================================

	// Normalize raw parental-control response object by converting UTC start_minute
	// and stop_minute back to subscriber-local start_time and stop_time (HH:MM),
	// shifting weekdays to match the local start day, and stripping internal fields.
	bool NormalizeScheduleResponse(Poco::JSON::Object::Ptr schedule, const std::string &timezoneStr) {

		if (!schedule || timezoneStr.empty() || !schedule->has("start_minute") || !schedule->has("stop_minute")) {
			return false;
		}

		auto &logger = Poco::Logger::get("ParentalControl");

		try {
			int startMinute = schedule->getValue<int>("start_minute");
			int stopMinute = schedule->getValue<int>("stop_minute");

			if (startMinute < 0 || startMinute >= 24 * 60 || stopMinute < 0 || stopMinute >= 24 * 60) {
				return false;
			}

			const auto *zone = date::locate_zone(timezoneStr);

			auto sysNow = std::chrono::system_clock::now();
			auto zonedNow = date::make_zoned(zone, sysNow);
			auto localDay = date::floor<date::days>(zonedNow.get_local_time());

			auto utcDay = date::floor<date::days>(zone->to_sys(localDay, date::choose::earliest));

			auto tentativeStart = utcDay + std::chrono::minutes(startMinute);
			auto tentativeLocalStart = date::floor<date::days>(date::make_zoned(zone, tentativeStart).get_local_time());

			utcDay += (localDay - tentativeLocalStart);

			auto sysStart = utcDay + std::chrono::minutes(startMinute);
			auto sysStopDays = (stopMinute <= startMinute) ? (utcDay + date::days(1)) : utcDay;
			auto sysStop = sysStopDays + std::chrono::minutes(stopMinute);


			auto localStartDt = date::make_zoned(zone, sysStart).get_local_time();
			auto localStopDt = date::make_zoned(zone, sysStop).get_local_time();

			auto localStartDays = date::floor<date::days>(localStartDt);
			auto localStopDays = date::floor<date::days>(localStopDt);

			startMinute = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(localStartDt - localStartDays).count());
			stopMinute = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(localStopDt - localStopDays).count());

			int startDayShift = static_cast<int>((localStartDays.time_since_epoch() - utcDay.time_since_epoch()).count());

			if (schedule->has("weekdays") && schedule->isArray("weekdays")) {
				auto weekdays = schedule->getArray("weekdays");
				ShiftWeekdays(weekdays, startDayShift);
				schedule->set("weekdays", weekdays);
			}

			std::string startTime;
			std::string stopTime;
			if (!MinuteToTimeString(startMinute, startTime) || !MinuteToTimeString(stopMinute, stopTime)) {
				return false;
			}

			schedule->set("start_time", startTime);
			schedule->set("stop_time", stopTime);
			schedule->remove("start_minute");
			schedule->remove("stop_minute");
			schedule->remove("config-raw");
			return true;
		} catch (const std::exception &e) {
			logger.error(fmt::format("Schedule response timezone conversion failed for timezone [{}]: {}", timezoneStr, e.what()));
			return false;
		} catch (...) {
			logger.error(fmt::format("Schedule response timezone conversion failed for timezone [{}]: unknown exception", timezoneStr));
			return false;
		}
	}

	// Parse an "HH:MM" string into minute of day (0..1439). Returns false if malformed.
	bool ParseTimeString(const Poco::Dynamic::Var &value, int &minuteOfDay) {
		if (!value.isString()) {
			return false;
		}

		const auto timeValue = value.convert<std::string>();
		if (timeValue.size() != 5 || timeValue[2] != ':' ||
			!std::isdigit(static_cast<unsigned char>(timeValue[0])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[1])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[3])) ||
			!std::isdigit(static_cast<unsigned char>(timeValue[4]))) {
			return false;
		}

		const int hour = (timeValue[0] - '0') * 10 + (timeValue[1] - '0');
		const int minute = (timeValue[3] - '0') * 10 + (timeValue[4] - '0');
		if (hour > 23 || minute > 59) {
			return false;
		}

		minuteOfDay = (hour * 60) + minute;
		return true;
	}

	// Validate that the weekdays array is non-empty and contains distinct values in 0..6.
	bool ValidateWeekdays(const Poco::JSON::Array::Ptr &weekdays) {
		if (!weekdays || weekdays->size() == 0) {
			return false;
		}

		std::set<int> seenDays;
		for (std::size_t i = 0; i < weekdays->size(); ++i) {
			try {
				const int day = weekdays->getElement<int>(i);
				if (day < 0 || day > 6 || !seenDays.insert(day).second) {
					return false;
				}
			} catch (...) {
				return false;
			}
		}
		return true;
	}

	// Parse and validate schedule POST/PUT JSON request body against schema rules.
	// On validation failure, sets HTTP 400 Bad Request error on handler and returns false.
	bool ParseAndValidateScheduleRequest(RESTAPIHandler &handler,

										  const Poco::JSON::Object::Ptr &body,
										  bool enabledRequired,
										  ParsedScheduleRequest &out) {
		std::vector<std::string> names;
		body->getNames(names);
		for (const auto &name : names) {
			if (name != "name" && name != "description" && name != "enabled" &&
				name != "action_type" && name != "target_kind" && name != "target_value" &&
				name != "start_time" && name != "stop_time" && name != "weekdays") {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "Unknown field: " + name);
				return false;
			}
		}

		if (!body->has("name") || body->isNull("name") || !body->get("name").isString()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters, "name is required");
			return false;
		}
		out.name = body->getValue<std::string>("name");
		Poco::trimInPlace(out.name);
		if (out.name.empty()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "name must be non-empty");
			return false;
		}

		if (!body->has("action_type") || body->isNull("action_type") ||
			!body->get("action_type").isString() ||
			body->getValue<std::string>("action_type") != "BLOCK") {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "action_type must be BLOCK");
			return false;
		}

		if (!body->has("target_kind") || body->isNull("target_kind") ||
			!body->get("target_kind").isString()) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "target_kind is required");
			return false;
		}
		out.targetKind = body->getValue<std::string>("target_kind");
		if (out.targetKind != "INTERNET" && out.targetKind != "APP") {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "target_kind must be INTERNET or APP");
			return false;
		}

		if (out.targetKind == "APP") {
			out.has_target_value = true;
			if (!body->has("target_value") || body->isNull("target_value") || !body->get("target_value").isString()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "APP schedules require a non-empty target_value");
				return false;
			}
			out.targetValue = body->getValue<std::string>("target_value");
			Poco::trimInPlace(out.targetValue);
			if (out.targetValue.empty()) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "APP schedules require a non-empty target_value");
				return false;
			}
		} else {
			if (body->has("target_value")) {
				out.has_target_value = true;
				if (!body->isNull("target_value")) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "INTERNET schedules require target_value to be null");
					return false;
				}
			} else {
				out.has_target_value = false;
			}
			out.targetValue = "";
		}

		if (!body->has("start_time") ||
			!ParseTimeString(body->get("start_time"), out.startMinute)) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "start_time must use HH:MM format");
			return false;
		}
		if (!body->has("stop_time") ||
			!ParseTimeString(body->get("stop_time"), out.stopMinute)) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "stop_time must use HH:MM format");
			return false;
		}
		if (out.startMinute == out.stopMinute) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "start_time and stop_time must not represent the same minute");
			return false;
		}

		if (!body->has("weekdays") || !body->isArray("weekdays") ||
			!ValidateWeekdays(body->getArray("weekdays"))) {
			handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
							   "weekdays must contain distinct values in the range 0..6");
			return false;
		}
		out.weekdays = body->getArray("weekdays");

		if (enabledRequired) {
			if (!body->has("enabled") || body->isNull("enabled") ||
				body->get("enabled").type() != typeid(bool)) {
				handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
								   "enabled is required and must be a boolean");
				return false;
			}
			out.enabled = body->getValue<bool>("enabled");
		} else {
			if (body->has("enabled")) {
				if (body->isNull("enabled") || body->get("enabled").type() != typeid(bool)) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "enabled must be a boolean");
					return false;
				}
				out.enabled = body->getValue<bool>("enabled");
			} else {
				out.enabled = true;
			}
		}

		if (body->has("description")) {
			out.has_description = true;
			if (body->isNull("description")) {
				out.description = std::nullopt;
			} else {
				if (!body->get("description").isString()) {
					handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,
									   "description must be a string or null");
					return false;
				}
				std::string desc = body->getValue<std::string>("description");
				Poco::trimInPlace(desc);
				out.description = std::move(desc);
			}
		} else {
			out.has_description = false;
			out.description = std::nullopt;
		}

		return true;
	}

	// Build the backend JSON request payload for mango-parental-control from a parsed schedule.
	Poco::JSON::Object BuildScheduleRequestBody(const ParsedScheduleRequest &req) {

		Poco::JSON::Object body;
		body.set("name", req.name);
		if (req.has_description) {
			if (req.description.has_value()) {
				body.set("description", *req.description);
			} else {
				body.set("description", Poco::Dynamic::Var());
			}
		}
		body.set("enabled", req.enabled);
		body.set("action_type", "BLOCK");
		body.set("target_kind", req.targetKind);
		if (req.targetKind == "APP") {
			body.set("target_value", req.targetValue);
		} else {
			if (req.has_target_value) {
				body.set("target_value", Poco::Dynamic::Var());
			}
		}
		body.set("start_minute", req.startMinute);
		body.set("stop_minute", req.stopMinute);
		body.set("weekdays", req.weekdays);
		return body;
	}

	// Resolve the IANA timezone string for a subscriber by fetching venue and location from OWProv.
	// On failure, sets the appropriate HTTP error on handler and returns false.
	bool ResolveSubscriberTimezone(RESTAPIHandler &handler, const std::string &subscriberId, std::string &timezone) {
		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::VenueList venueList;

		if (!SDK::Prov::Venue::GetVenues(&handler, subscriberId, venueList, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				handler.ForwardErrorResponse(&handler, callStatus, callResponse);
				return false;
			}
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}

		if (venueList.venues.empty()) {
			handler.BadRequest(RESTAPI::Errors::TimezoneRequired);
			return false;
		}

		const auto &venue = venueList.venues[0];
		if (venue.location.empty()) {
			handler.BadRequest(RESTAPI::Errors::TimezoneRequired);
			return false;
		}

		callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::Location location;

		if (!SDK::Prov::Location::Get(&handler, venue.location, location, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				handler.ForwardErrorResponse(&handler, callStatus, callResponse);
				return false;
			}
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}

		if (location.timezone.empty()) {
			handler.BadRequest(RESTAPI::Errors::TimezoneRequired);
			return false;
		}

		timezone = location.timezone;
		auto &logger = Poco::Logger::get("ParentalControl");
		logger.information(fmt::format("Resolved subscriber [{}] venue location timezone: [{}]", subscriberId, timezone));
		return true;
	}

	// Convert request.startMinute and request.stopMinute from the local timezone to UTC in-place.
	// Handles overnight schedules and shifts weekdays when the UTC calendar day differs from local day.
	bool ConvertScheduleTimesToUtc(const std::string &timezoneStr, ParsedScheduleRequest &request) {

		if (timezoneStr.empty()) {
			return false;
		}

		auto &logger = Poco::Logger::get("ParentalControl");
		const int originalStartMinute = request.startMinute;
		const int originalStopMinute = request.stopMinute;

		try {
			const auto *zone = date::locate_zone(timezoneStr);

			// Resolve current calendar date in the subscriber's local timezone.
			auto sysNow = std::chrono::system_clock::now();
			auto zonedNow = date::make_zoned(zone, sysNow);
			auto localDate = date::floor<date::days>(zonedNow.get_local_time());

			// Build local start time point from today's local date.
			auto localStart = localDate + std::chrono::hours(request.startMinute / 60) + std::chrono::minutes(request.startMinute % 60);

			// Overnight schedule: stopMinute <= startMinute means stop is on the
			// next local calendar day.
			auto localStopDate = (request.stopMinute <= request.startMinute) ? (localDate + date::days(1)) : localDate;
			auto localStop = localStopDate + std::chrono::hours(request.stopMinute / 60) + std::chrono::minutes(request.stopMinute % 60);

			// Convert to system (UTC) time. Use choose::earliest to resolve
			// ambiguous local times (DST fall-back) deterministically without
			// throwing.
			auto sysStart = zone->to_sys(localStart, date::choose::earliest);
			auto sysStop = zone->to_sys(localStop, date::choose::earliest);

			// Extract UTC minute-of-day via explicit duration_cast<minutes>.
			auto sysStartDays = date::floor<date::days>(sysStart);
			int utcStartMin = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(sysStart - sysStartDays).count());

			auto sysStopDays = date::floor<date::days>(sysStop);
			int utcStopMin = static_cast<int>(std::chrono::duration_cast<std::chrono::minutes>(sysStop - sysStopDays).count());

			// Day-shift calculation using time_since_epoch() to avoid mixing
			// incompatible local_days / sys_days clock types.
			int startDayShift = static_cast<int>((sysStartDays.time_since_epoch() - localDate.time_since_epoch()).count());

			request.startMinute = utcStartMin;
			request.stopMinute = utcStopMin;

			// Shift weekdays by startDayShift. weekdays represent the schedule's
			// start day, so only startDayShift is applied.
			ShiftWeekdays(request.weekdays, startDayShift);

			logger.information(fmt::format("Converting schedule time for timezone [{}]: local start {:02d}:{:02d} ({}) -> UTC {:02d}:{:02d} ({}), local stop {:02d}:{:02d} ({}) -> UTC {:02d}:{:02d} ({}), startDayShift={}",
				timezoneStr,
				originalStartMinute / 60, originalStartMinute % 60, originalStartMinute,
				utcStartMin / 60, utcStartMin % 60, utcStartMin,
				originalStopMinute / 60, originalStopMinute % 60, originalStopMinute,
				utcStopMin / 60, utcStopMin % 60, utcStopMin,
				startDayShift));

			return true;
		} catch (const std::exception &e) {
			logger.error(fmt::format("Schedule timezone conversion failed for timezone [{}], startMinute={}, stopMinute={}: {}", timezoneStr, originalStartMinute, originalStopMinute, e.what()));
			return false;
		} catch (...) {
			logger.error(fmt::format("Schedule timezone conversion failed for timezone [{}], startMinute={}, stopMinute={}: unknown exception", timezoneStr, originalStartMinute, originalStopMinute));
			return false;
		}
	}

	// =========================================================================
	// Topology/Device Validation Helpers
	// =========================================================================

	ValidateMacResult ValidateMacInTopology(RESTAPIHandler &handler,
											const std::string &subscriberId,
											const std::string &operatorId,
											const std::string &clientMac,
											std::string &gatewaySerial) {
		ValidateMacResult result = ResolveGatewaySerial(handler, subscriberId, operatorId, gatewaySerial);
		if (result != ValidateMacResult::Success) {
			return result;
		}

		std::string boardId;
		result = ResolveVenueBoards(handler, gatewaySerial, boardId);
		if (result != ValidateMacResult::Success) {
			return result;
		}

		Poco::JSON::Object::Ptr topologyResponse;
		result = FetchBoardTopology(handler, boardId, topologyResponse);
		if (result != ValidateMacResult::Success) {
			return result;
		}

		return MacPresentInTopologyPayload(topologyResponse, clientMac);
	}

	// =========================================================================
	// Config-Raw Extraction/Apply Helpers
	// =========================================================================

	bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &callResponse,
								  Poco::JSON::Array::Ptr &configRaw,
								  bool required) {
		configRaw.reset();
		if (!callResponse) {
			return !required;
		}
		if (!callResponse->has("config-raw")) {
			return !required;
		}
		if (callResponse->isNull("config-raw")) {
			return true;
		}
		if (!callResponse->isArray("config-raw")) {
			return false;
		}
		configRaw = callResponse->getArray("config-raw");
		if (!configRaw) {
			return false;
		}
		for (std::size_t i = 0; i < configRaw->size(); ++i) {
			try {
				auto cmd = configRaw->getArray(i);
				if (!cmd) {
					return false;
				}
				if (cmd->size() != 2 && cmd->size() != 3) {
					return false;
				}
				for (std::size_t j = 0; j < cmd->size(); ++j) {
					if (!cmd->get(j).isString()) {
						return false;
					}
				}
			} catch (...) {
				return false;
			}
		}
		return true;
	}

	ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &handler, Poco::Logger &logger,
										const std::string &subscriberId,
										const std::string &operatorId, const std::string &objectId,
										const Poco::JSON::Array::Ptr &configRaw,
										const std::string &operationName,
										const std::string &objectType,
										const std::string &gatewaySerial) {
		if (!configRaw) {
			return ApplyConfigRawResult::NoConfigApplyNeeded;
		}

		if (operatorId.empty()) {
			logger.error(fmt::format("{}: operator id missing for gateway apply (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingOperatorId;
		}

		std::string resolvedSerial = gatewaySerial;
		if (resolvedSerial.empty()) {
			ProvObjects::SubscriberDeviceList devList;
			Poco::Net::HTTPResponse::HTTPStatus provStatus;
			Poco::JSON::Object::Ptr provResponse;
			if (!SDK::Prov::Subscriber::GetDevices(&handler, subscriberId, operatorId, devList, provStatus, provResponse)) {
				logger.error(fmt::format("{}: provisioning lookup failed (subscriber={} {}={})",
										 operationName, subscriberId, objectType, objectId));
				return ApplyConfigRawResult::ProvisioningLookupFailed;
			}

			for (const auto &dev : devList.subscriberDevices) {
				std::string grp = dev.deviceGroup;
				Poco::toLowerInPlace(grp);
				if (grp == "olg") {
					resolvedSerial = dev.serialNumber;
					break;
				}
			}
		}

		if (resolvedSerial.empty()) {
			logger.error(fmt::format("{}: gateway serial not resolved (subscriber={} {}={})",
									 operationName, subscriberId, objectType, objectId));
			return ApplyConfigRawResult::MissingGatewaySerial;
		}

		Poco::JSON::Object::Ptr gwResponse;
		Poco::Net::HTTPResponse::HTTPStatus gwStatus;
		if (!SDK::GW::Device::GetConfig(&handler, resolvedSerial, gwStatus, gwResponse)) {
			if (gwStatus == Poco::Net::HTTPResponse::HTTP_OK) {
				logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
										 resolvedSerial));
			} else {
				logger.error(fmt::format("{}: gateway config load failed (serial={})",
										 operationName, resolvedSerial));
			}
			return ApplyConfigRawResult::GatewayConfigLoadFailed;
		}

		if (!gwResponse || !gwResponse->has("configuration") ||
			!gwResponse->isObject("configuration")) {
			logger.error(fmt::format("{}: gateway config malformed (serial={})", operationName,
									 resolvedSerial));
			return ApplyConfigRawResult::GatewayConfigMalformed;
		}

		auto gatewayConfig = gwResponse->getObject("configuration");
		// Replacing the full config-raw section is intentional because parental-control is
		// currently the only service producing config-raw, and the gateway-fetched config-raw
		// doesn't include a reliable ownership marker to enable selective merging.
		gatewayConfig->set("config-raw", configRaw);

		Poco::JSON::Object::Ptr configureResponse;
		Poco::Net::HTTPResponse::HTTPStatus configureStatus;
		if (!SDK::GW::Device::Configure(&handler, resolvedSerial, gatewayConfig, configureStatus,
										configureResponse)) {
			logger.error(fmt::format("{}: gateway configure failed (serial={})", operationName,
									 resolvedSerial));
			return ApplyConfigRawResult::GatewayConfigureFailed;
		}

		return ApplyConfigRawResult::Applied;
	}

	// =========================================================================
	// HTTP/Result Mapping Helpers
	// =========================================================================

	bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result) {
		switch (result) {
		case ApplyConfigRawResult::NoConfigApplyNeeded:
		case ApplyConfigRawResult::Applied:
			return true;
		case ApplyConfigRawResult::MissingOperatorId:
			handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
			return false;
		case ApplyConfigRawResult::MissingGatewaySerial:
			handler.InternalError(RESTAPI::Errors::MissingSerialNumber);
			return false;
		case ApplyConfigRawResult::ProvisioningLookupFailed:
		case ApplyConfigRawResult::GatewayConfigLoadFailed:
		case ApplyConfigRawResult::GatewayConfigMalformed:
		case ApplyConfigRawResult::GatewayConfigureFailed:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		handler.InternalError(RESTAPI::Errors::InternalError);
		return false;
	}

	bool HandleValidateMacResult(RESTAPIHandler &handler, ValidateMacResult result) {
		switch (result) {
		case ValidateMacResult::Success:
			return true;
		case ValidateMacResult::MissingSubscriberOrOperator:
			handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
			return false;
		case ValidateMacResult::SubscriberDevicesNotFound:
		case ValidateMacResult::GatewaySerialNotFound:
			handler.BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		case ValidateMacResult::ProvisioningLookupFailed:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		case ValidateMacResult::InventoryNotFound:
			handler.BadRequest(RESTAPI::Errors::GatewayInventoryNotFound);
			return false;
		case ValidateMacResult::VenueNotFound:
			handler.BadRequest(RESTAPI::Errors::VenueMustExist);
			return false;
		case ValidateMacResult::VenueLookupFailed:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		case ValidateMacResult::BoardIdNotFound:
			handler.BadRequest(RESTAPI::Errors::VenueMissingBoardId);
			return false;
		case ValidateMacResult::TopologyNotFound:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		case ValidateMacResult::MacNotPresentInTopology:
			handler.BadRequest(RESTAPI::Errors::MacNotPresentInTopology);
			return false;
		case ValidateMacResult::TopologyUnusable:
			handler.InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		handler.InternalError(RESTAPI::Errors::InternalError);
		return false;
	}
	// =========================================================================
	// Blocked-Client Evaluation (used by topology response)
	// =========================================================================

	namespace {
		struct FirewallRuleInfo {
			bool enabled = false;
			bool hasSection = false;
			bool hasEnabled = false;
			bool hasStartDate = false;
			bool hasStopDate = false;
			bool hasStartTime = false;
			bool hasStopTime = false;
			std::string startDate;
			std::string stopDate;
			std::string startTime;
			std::string stopTime;
			std::vector<std::string> macs;
		};

		bool IsValidDate(const std::string &date) {
			if (date.size() != 10) return false;
			Poco::DateTime dt;
			int tz = 0;
			return Poco::DateTimeParser::tryParse("%Y-%m-%d", date, dt, tz);
		}

		bool IsValidTime(const std::string &time) {
			if (time.size() != 8) return false;
			Poco::DateTime dt;
			int tz = 0;
			return Poco::DateTimeParser::tryParse("%H:%M:%S", time, dt, tz);
		}

		/*
			IsRuleActive: Checks if a client-access firewall block rule is currently active.
			Client-access rules have start_date/stop_date + start_time/stop_time.
			Active if current date/time is between start date/time and stop date/time.
			For same-day time windows (stopTime >= startTime), the stop threshold uses
			startDate because stop_date is intentionally set to the next calendar date
			by mango-parental-control.
		*/
		bool IsRuleActive(const std::string &nowDateStr, const std::string &nowTimeStr, const FirewallRuleInfo &rule) {
			if (!rule.hasSection || !rule.hasEnabled || !rule.enabled) {
				return false;
			}

			if (!rule.hasStartDate || !rule.hasStopDate || !rule.hasStartTime || !rule.hasStopTime) {
				return false;
			}

			if (rule.macs.empty()) {
				return false;
			}

			std::string nowStr = nowDateStr + " " + nowTimeStr;
			std::string startStr = rule.startDate + " " + rule.startTime;
			if (nowStr < startStr) {
				return false;
			}

			// For same-day time windows (stopTime >= startTime), the daily stop
			// threshold uses startDate because stop_date is intentionally set to
			// the next calendar date by mango-parental-control.
			std::string stopDate = rule.stopDate;
			if (rule.stopTime >= rule.startTime) {
				stopDate = rule.startDate;
			}

			std::string stopStr = stopDate + " " + rule.stopTime;
			if (nowStr > stopStr) {
				return false;
			}

			return true;
		}
	} // namespace

	bool GetBlockedClients(const Poco::JSON::Object::Ptr &config,
						   std::list<std::string> &blockedMacs) {
		blockedMacs.clear();
		if (!config || !config->has("config-raw") || !config->isArray("config-raw")) {
			return config != nullptr;
		}

		auto configRaw = config->getArray("config-raw");
		if (!configRaw) {
			return true;
		}

		Poco::DateTime now;
		std::string nowDateStr = Poco::DateTimeFormatter::format(now, "%Y-%m-%d");
		std::string nowTimeStr = Poco::DateTimeFormatter::format(now, "%H:%M:%S");

		std::map<std::string, FirewallRuleInfo> rules;
		for (std::size_t i = 0; i < configRaw->size(); ++i) {
			try {
				auto cmd = configRaw->getArray(i);
				if (!cmd || cmd->size() != 3)
					continue;

				auto op = cmd->getElement<std::string>(0);
				auto key = cmd->getElement<std::string>(1);
				auto val = cmd->getElement<std::string>(2);

				if (key.rfind("firewall.pc_client_access_", 0) == 0) {
					std::string prefix = "firewall.pc_client_access_";
					auto nextDot = key.find('.', prefix.size());
					if (nextDot == std::string::npos) {
						if (op == "set" && val == "rule") {
							rules[key].hasSection = true;
						}
					} else {
						std::string section = key.substr(0, nextDot);
						std::string param = key.substr(nextDot + 1);

						if (op == "set") {
							if (param == "enabled") {
								rules[section].enabled = (val == "1");
								rules[section].hasEnabled = true;
							} else if (param == "start_date") {
								rules[section].startDate = val;
								rules[section].hasStartDate = IsValidDate(val);
							} else if (param == "stop_date") {
								rules[section].stopDate = val;
								rules[section].hasStopDate = IsValidDate(val);
							} else if (param == "start_time") {
								rules[section].startTime = val;
								rules[section].hasStartTime = IsValidTime(val);
							} else if (param == "stop_time") {
								rules[section].stopTime = val;
								rules[section].hasStopTime = IsValidTime(val);
							}
						} else if (op == "add_list" && param == "src_mac") {
							std::string normalizedMac = val;
							if (Utils::NormalizeMac(normalizedMac)) {
								rules[section].macs.push_back(normalizedMac);
							}
						}
					}
				}
			} catch (...) {
				continue;
			}
		}

		auto &logger = Poco::Logger::get("ParentalControl");
		for (const auto &[section, rule] : rules) {
			if (IsRuleActive(nowDateStr, nowTimeStr, rule)) {
				for (auto mac : rule.macs) {
					if (Utils::NormalizeMac(mac)) {
						blockedMacs.push_back(mac);
						logger.debug(fmt::format("Active blocked client MAC found: {}", Utils::SerialToMAC(mac)));
					}
				}
			}
		}
		return true;
	}

} // namespace OpenWifi::RESTAPI::ParentalControl
