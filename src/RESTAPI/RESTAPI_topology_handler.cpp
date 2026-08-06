/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
#include <list>
#include <unordered_map>
#include <unordered_set>
#include "framework/utils.h"
#include "sdks/SDK_gw.h"

#include "RESTAPI_topology_handler.h"
#include "RESTAPI_parental_control_utils.h"

#include "Poco/String.h"
#include "framework/ow_constants.h"
#include "sdks/SDK_nw_topology.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {
	bool RESTAPI_topology_handler::FetchSubscriberDevices(
		ProvObjects::SubscriberDeviceList &subscriberDevices) {
		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();

		if (SDK::Prov::Subscriber::GetDevices(nullptr, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
											  subscriberDevices, callStatus, callResponse)) {
			return true;
		}

		if (callStatus == Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] No subscriber devices found for subscriber {}.",
									   UserInfo_.userinfo.id));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}

		if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
			Logger().error(
				fmt::format("[GET-TOPOLOGY] Failed to fetch subscriber devices for subscriber {} "
							"(status {}).",
							UserInfo_.userinfo.id, static_cast<uint32_t>(callStatus)));
			ForwardErrorResponse(this, callStatus, callResponse);
			return false;
		}

		Logger().error(fmt::format("[GET-TOPOLOGY] Failed to parse subscriber devices payload for "
								   "subscriber {}.",
								   UserInfo_.userinfo.id));
		InternalError(RESTAPI::Errors::InternalError);
		return false;
	}

	bool RESTAPI_topology_handler::FindGatewaySerial(
		const ProvObjects::SubscriberDeviceList &subscriberDevices, std::string &gatewaySerial) {
		for (const auto &device : subscriberDevices.subscriberDevices) {
			auto group = device.deviceGroup;
			Poco::toLowerInPlace(group);
			if (group == "olg") {
				gatewaySerial = device.serialNumber;
				break;
			}
		}

		if (gatewaySerial.empty()) {
			Logger().debug(fmt::format(
				"[GET-TOPOLOGY] No gateway device (deviceGroup=olg) found for subscriber {}.",
				UserInfo_.userinfo.id));
			Poco::JSON::Object response;
			ReturnObject(response);
			return false;
		}
		return true;
	}

	bool RESTAPI_topology_handler::ResolveVenueTopologyContext(const std::string &gatewaySerial, VenueTopologyContext &context) {
		context.boardId.clear();
		context.timezone.clear();

		if (gatewaySerial.empty()) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Gateway serial is empty for subscriber {}.",
									   UserInfo_.userinfo.id));
			BadRequest(RESTAPI::Errors::MissingSerialNumber);
			return false;
		}

		ProvObjects::InventoryTag inventory;
		if (!SDK::Prov::Device::Get(nullptr, gatewaySerial, inventory)) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Inventory record missing for device: {}.",
									   gatewaySerial));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}

		if (inventory.venue.empty()) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Inventory has no venue for device: {}.",
									   gatewaySerial));
			BadRequest(RESTAPI::Errors::VenueMustExist);
			return false;
		}

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::Venue venue;
		if (!SDK::Prov::Venue::Get(nullptr, inventory.venue, venue, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().error(fmt::format("[GET-TOPOLOGY] Failed to fetch venue {} (status {}).",
										   inventory.venue, static_cast<uint32_t>(callStatus)));
				ForwardErrorResponse(this, callStatus, callResponse);
				return false;
			}
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] Failed to parse venue {} response.", inventory.venue));
			InternalError(RESTAPI::Errors::InternalError);
			return false;
		}

		if (venue.boards.empty()) {
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] No boards found for venue {}.", inventory.venue));
			NotFound();
			return false;
		}

		context.boardId = venue.boards.front();
		if (context.boardId.empty()) {
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] Empty board id found for venue {}.", inventory.venue));
			NotFound();
			return false;
		}

		if (venue.location.empty()) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Venue {} has no location configured for subscriber {}.", inventory.venue, UserInfo_.userinfo.id));
			BadRequest(RESTAPI::Errors::TimezoneRequired);
			return false;
		}

		ProvObjects::Location location;
		callStatus = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		callResponse = Poco::makeShared<Poco::JSON::Object>();
		if (SDK::Prov::Location::Get(nullptr, venue.location, location, callStatus, callResponse)) {
			if (location.timezone.empty()) {
				Logger().debug(fmt::format("[GET-TOPOLOGY] Location {} has no timezone configured for subscriber {}.", venue.location, UserInfo_.userinfo.id));
				BadRequest(RESTAPI::Errors::TimezoneRequired);
				return false;
			}
			context.timezone = location.timezone;
			Logger().debug(fmt::format("[GET-TOPOLOGY] Resolved venue timezone [{}] for subscriber {}.", context.timezone, UserInfo_.userinfo.id));
			return true;
		}

		if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
			Logger().error(fmt::format("[GET-TOPOLOGY] OWProv Location lookup failed for location {}.", venue.location));
			ForwardErrorResponse(this, callStatus, callResponse);
			return false;
		}

		Logger().error(fmt::format("[GET-TOPOLOGY] Failed to parse location {} response.", venue.location));
		InternalError(RESTAPI::Errors::InternalError);
		return false;
	}

	bool RESTAPI_topology_handler::FetchTopology(const std::string &boardId,
												 Poco::JSON::Object::Ptr &topologyResponse) {
		Poco::Net::HTTPServerResponse::HTTPStatus topoStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		topologyResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Topology::Get(nullptr, boardId, topoStatus, topologyResponse)) {
			if (topoStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().debug(
					fmt::format("[GET-TOPOLOGY] Failed to fetch topology for board {} (status {}).",
								boardId, static_cast<uint32_t>(topoStatus)));
				ForwardErrorResponse(this, topoStatus, topologyResponse);
				return false;
			}
			Logger().debug(fmt::format(
				"[GET-TOPOLOGY] Failed to parse topology response for board {}.", boardId));
			InternalError(RESTAPI::Errors::InternalError);
			return false;
		}
		return true;
	}


	/*
		FinalizeTopologyResponse:
		1. Filter topology nodes based on subscriber devices.
		2. Fetch blocked MACs from the gateway configuration.
		3. Attach a "blocked" flag to historical clients and live client entries in the topology.
		4. Attach the venue timezone to the topology response.
	*/
	void RESTAPI_topology_handler::FinalizeTopologyResponse(const ProvObjects::SubscriberDeviceList &subscriberDevices,
		const std::string &gatewaySerial, const VenueTopologyContext &context, Poco::JSON::Object::Ptr &topologyResponse) {
		if (!topologyResponse)
			return;

		FilterTopologyNodes(subscriberDevices, topologyResponse);
		FilterTopologyEdges(subscriberDevices, topologyResponse);
		TagBlockedClients(gatewaySerial, topologyResponse, context.timezone);
		topologyResponse->set("timezone", context.timezone);
	}

	static std::unordered_set<std::string>BuildAllowedSerialsSet(const ProvObjects::SubscriberDeviceList &subscriberDevices) {
		std::unordered_set<std::string> allowedSerials;
		allowedSerials.reserve(subscriberDevices.subscriberDevices.size());
		for (const auto &device : subscriberDevices.subscriberDevices) {
			if (device.serialNumber.empty())
				continue;
			allowedSerials.insert(device.serialNumber);
		}
		return allowedSerials;
	}

	void RESTAPI_topology_handler::FilterTopologyEdges(
		const ProvObjects::SubscriberDeviceList &subscriberDevices,
		Poco::JSON::Object::Ptr &topologyResponse) {
		if (!topologyResponse || !topologyResponse->has("edges") || !topologyResponse->isObject("edges")) {
			return;
		}
		auto allowedSerials = BuildAllowedSerialsSet(subscriberDevices);
		auto edges = topologyResponse->getObject("edges");
		const std::vector<std::string> edgeTypes{"mesh", "wired"};
		for (const auto &edgeType : edgeTypes) {
			if (!edges->has(edgeType) || !edges->isArray(edgeType))
				continue;

			auto edgeArray = edges->getArray(edgeType);
			auto filteredEdges = Poco::makeShared<Poco::JSON::Array>();
			for (std::size_t i = 0; i < edgeArray->size(); ++i) {
				auto edge = edgeArray->getObject(i);
				if (!edge || !edge->has("from") || !edge->get("from").isString()|| !edge->has("to") || !edge->get("to").isString())
					continue;

				auto from = edge->getValue<std::string>("from");
				auto to = edge->getValue<std::string>("to");
				if (allowedSerials.find(from) != allowedSerials.end() && allowedSerials.find(to) != allowedSerials.end()) {
					filteredEdges->add(edge);
				}
			}
			edges->set(edgeType, filteredEdges);
		}
	}

	void RESTAPI_topology_handler::FilterTopologyNodes(
		const ProvObjects::SubscriberDeviceList &subscriberDevices,
		Poco::JSON::Object::Ptr &topologyResponse) {
		if (!topologyResponse || !topologyResponse->has("nodes") || !topologyResponse->isArray("nodes")) {
			return;
		}
		auto allowedSerials = BuildAllowedSerialsSet(subscriberDevices);
		auto nodes = topologyResponse->getArray("nodes");
		auto filteredNodes = Poco::makeShared<Poco::JSON::Array>();
		for (std::size_t i = 0; i < nodes->size(); ++i) {
			auto node = nodes->getObject(i);
			if (!node || !node->has("serial") || !node->get("serial").isString())
				continue;

			auto serial = node->getValue<std::string>("serial");
			if (allowedSerials.find(serial) != allowedSerials.end())
				filteredNodes->add(node);
		}
		topologyResponse->set("nodes", filteredNodes);
	}

	/*
		TagBlockedClients:
		1. Fetch blocked MACs from the gateway configuration.
		2. Attach a "blocked" flag to historical clients and live client entries in the topology.

		Required Topology Response:-
		{
		"edges": {
			"mesh": [
			{
				...
				"from": "f0090d2db49c",
				"ssid": "Mesh-SSID",
				"to": "dc62796520cd"
			},
			{
				...
				"from": "dc62796520cd",
				"ssid": "Mesh-SSID",
				"to": "f0090d2db49c"
			},
			"wired": []
		},
		{
			"historicalClients": [
			{
				"blocked": "0",
				"station": "3a:6e:34:8a:a1:d6"
			},
			{
				"blocked": "1",
				"station": "56:36:22:9d:b9:af"
			}
			]
		},
		"nodes": [
			{
				"aps": [
					{
						"band": "5",
						"bssid": "f0:09:0d:2d:b4:9b",
						"clients": [
							{
								"blocked": "1",
								...
								"station": "6c:c7:ec:de:10:65"
							},
							{
								"blocked": "1",
								...
								"station": "94:97:ae:f7:44:fd"
							}
						],
					"mode": "ap",
					"ssid": "OpenWiFi-SSID"
					}
				],
				"connected": true,
				"mesh": [...],
				"serial": "f0090d2db49c"
			}
		]
		}
	*/
	void RESTAPI_topology_handler::TagBlockedClients(const std::string &gatewaySerial, Poco::JSON::Object::Ptr &topologyResponse, const std::string &timezoneStr) {

		Poco::JSON::Object::Ptr deviceObj;
		Poco::Net::HTTPServerResponse::HTTPStatus status = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;

		const bool gotConfig = SDK::GW::Device::GetConfig(nullptr, gatewaySerial, status, deviceObj);

		Poco::JSON::Object::Ptr config;
		if (gotConfig && deviceObj && deviceObj->has("configuration") && deviceObj->isObject("configuration")) {
			config = deviceObj->getObject("configuration");
		}

		std::map<std::string, std::string> blockedMacMap;
		if (!config || !RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacMap, timezoneStr)) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Failed to fetch config for {}.", gatewaySerial));
		}

		std::unordered_map<std::string, std::string> blockedMacSet;
		blockedMacSet.reserve(blockedMacMap.size());
		for (const auto &[macNorm, untilStr] : blockedMacMap) {
			blockedMacSet[Utils::SerialToMAC(macNorm)] = untilStr;
		}

		if (auto historicalDevices = topologyResponse->getArray("historicalDevices")) {
			auto historicalClientsWithFlags = Poco::makeShared<Poco::JSON::Array>();
			for (std::size_t i = 0; i < historicalDevices->size(); ++i) {
				std::string station;
				try {
					station = historicalDevices->getElement<std::string>(i);
				} catch (...) {
					continue;
				}
				std::string normalizedStation = station;
				Poco::toLowerInPlace(normalizedStation);
				auto entry = Poco::makeShared<Poco::JSON::Object>();
				entry->set("station", station);
				const auto it = blockedMacSet.find(normalizedStation);
				if (it != blockedMacSet.end()) {
					entry->set("blocked", "1");
					entry->set("blocked_until", it->second);
				} else {
					entry->set("blocked", "0");
					entry->set("blocked_until", "");
				}
				historicalClientsWithFlags->add(entry);
			}
			topologyResponse->set("historicalClients", historicalClientsWithFlags);
			topologyResponse->remove("historicalDevices");
		}

		if (auto nodes = topologyResponse->getArray("nodes")) {
			for (std::size_t i = 0; i < nodes->size(); ++i) {
				auto node = nodes->getObject(i);
				auto aps = node->getArray("aps");
				if (!aps) {
					continue;
				}
				for (std::size_t apIndex = 0; apIndex < aps->size(); ++apIndex) {
					auto ap = aps->getObject(apIndex);
					if (!ap) {
						continue;
					}
					auto clients = ap->getArray("clients");
					if (!clients) {
						continue;
					}
					for (std::size_t clientIndex = 0; clientIndex < clients->size(); ++clientIndex) {
						auto client = clients->getObject(clientIndex);
						if (!client || !client->has("station") || !client->get("station").isString()) {
							continue;
						}
						const auto station = client->getValue<std::string>("station");
						std::string normalizedStation = station;
						Poco::toLowerInPlace(normalizedStation);
						const auto it = blockedMacSet.find(normalizedStation);
						if (it != blockedMacSet.end()) {
							client->set("blocked", "1");
							client->set("blocked_until", it->second);
						} else {
							client->set("blocked", "0");
							client->set("blocked_until", "");
						}
					}
				}
			}
		}
	}

	void RESTAPI_topology_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().debug("[GET-TOPOLOGY] Received topology request without subscriber id.");
			return NotFound();
		}

		ProvObjects::SubscriberDeviceList subscriberDevices;
		if (!FetchSubscriberDevices(subscriberDevices))
			return;

		std::string gatewaySerial;
		if (!FindGatewaySerial(subscriberDevices, gatewaySerial))
			return;

		VenueTopologyContext context;
		if (!ResolveVenueTopologyContext(gatewaySerial, context))
			return;

		Poco::JSON::Object::Ptr topologyResponse;
		if (!FetchTopology(context.boardId, topologyResponse))
			return;

		FinalizeTopologyResponse(subscriberDevices, gatewaySerial, context, topologyResponse);

		return ReturnObject(*topologyResponse);
	}
} // namespace OpenWifi
