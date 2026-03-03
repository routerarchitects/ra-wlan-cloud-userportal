/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
#include <list>
#include <unordered_set>
#include "framework/utils.h"
#include "sdks/SDK_gw.h"

#include "RESTAPI_topology_handler.h"

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

	bool RESTAPI_topology_handler::ResolveBoardIdFromGateway(const std::string &gatewaySerial,
															 std::string &boardId) {
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

		boardId = venue.boards.front();
		if (boardId.empty()) {
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] Empty board id found for venue {}.", inventory.venue));
			NotFound();
			return false;
		}
		return true;
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
	*/
	void RESTAPI_topology_handler::FinalizeTopologyResponse(const ProvObjects::SubscriberDeviceList &subscriberDevices,
		const std::string &gatewaySerial, Poco::JSON::Object::Ptr &topologyResponse) {
		if (!topologyResponse)
			return;

		FilterTopologyNodes(subscriberDevices, topologyResponse);
		MarkBlockedClients(gatewaySerial, topologyResponse);
	}

	void RESTAPI_topology_handler::FilterTopologyNodes(
		const ProvObjects::SubscriberDeviceList &subscriberDevices,
		Poco::JSON::Object::Ptr &topologyResponse) {
		if (!topologyResponse || !topologyResponse->has("nodes") || !topologyResponse->isArray("nodes")) {
			return;
		}

		std::unordered_set<std::string> allowedSerials;
		allowedSerials.reserve(subscriberDevices.subscriberDevices.size());
		for (const auto &device : subscriberDevices.subscriberDevices) {
			if (device.serialNumber.empty())
				continue;
			auto serial = device.serialNumber;
			if (!Utils::NormalizeMac(serial)) {
				Poco::toLowerInPlace(serial);
			}
			allowedSerials.insert(serial);
		}

		auto nodes = topologyResponse->getArray("nodes");
		auto filteredNodes = Poco::makeShared<Poco::JSON::Array>();
		for (std::size_t i = 0; i < nodes->size(); ++i) {
			auto node = nodes->getObject(i);
			if (!node || !node->has("serial") || !node->get("serial").isString())
				continue;

			auto serial = node->getValue<std::string>("serial");
			if (!Utils::NormalizeMac(serial)) {
				Poco::toLowerInPlace(serial);
			}
			if (allowedSerials.find(serial) != allowedSerials.end())
				filteredNodes->add(node);
		}
		topologyResponse->set("nodes", filteredNodes);
	}

	/*
		MarkBlockedClients:
		1. Fetch blocked MACs from the gateway configuration.
		2. Attach a "blocked" flag to historical clients and live client entries in the topology.
	*/
	void RESTAPI_topology_handler::MarkBlockedClients(const std::string &gatewaySerial, Poco::JSON::Object::Ptr &topologyResponse) {

		std::list<std::string> blockedMacs;
		Poco::JSON::Object::Ptr deviceObj;
		Poco::Net::HTTPServerResponse::HTTPStatus status = Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;

		const bool gotConfig = SDK::GW::Device::GetConfig(nullptr, gatewaySerial, status, deviceObj);

		Poco::JSON::Object::Ptr config;
		if (gotConfig && deviceObj && deviceObj->has("configuration") && deviceObj->isObject("configuration")) {
			config = deviceObj->getObject("configuration");
		}

		if (!config || !SDK::GW::Device::GetBlockedClients(config, blockedMacs)) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Failed to fetch config for {}.", gatewaySerial));
		}

		std::unordered_set<std::string> blockedMacSet;
		blockedMacSet.reserve(blockedMacs.size());
		for (const auto &macNorm : blockedMacs) {
			blockedMacSet.insert(Utils::SerialToMAC(macNorm));
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
				auto entry = Poco::makeShared<Poco::JSON::Object>();
				entry->set("station", station);
				entry->set("blocked", blockedMacSet.count(station) ? "1" : "0");
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
						client->set("blocked", blockedMacSet.count(station) ? "1" : "0");
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

		std::string boardId;
		if (!ResolveBoardIdFromGateway(gatewaySerial, boardId))
			return;

		Poco::JSON::Object::Ptr topologyResponse;
		if (!FetchTopology(boardId, topologyResponse))
			return;

		FinalizeTopologyResponse(subscriberDevices, gatewaySerial, topologyResponse);

		return ReturnObject(*topologyResponse);
	}
} // namespace OpenWifi
