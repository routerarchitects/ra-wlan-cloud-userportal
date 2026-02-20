/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
#include <unordered_set>
#include "sdks/SDK_gw.h"

#include "RESTAPI_topology_handler.h"


#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/ow_constants.h"
#include "framework/utils.h"
#include "sdks/SDK_nw_topology.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {

	void RESTAPI_topology_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().debug("[GET-TOPOLOGY] Received topology request without subscriber id.");
			return NotFound();
		}

		SubObjects::SubscriberInfo subscriberInfo;
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, subscriberInfo)) {
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] Subscriber {} not found for topology request.",
							UserInfo_.userinfo.id));
			return NotFound();
		}

		if (subscriberInfo.accessPoints.list.empty()) {
			Logger().debug(fmt::format(
				"[GET-TOPOLOGY] Subscriber {} has no access points for topology request.",
				UserInfo_.userinfo.id));
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		// Use the first access point for topology request
		const auto &device = subscriberInfo.accessPoints.list.front();
		if (device.macAddress.empty()) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Subscriber {} access point has no MAC "
									   "address for topology request.",
									   UserInfo_.userinfo.id));
			return BadRequest(RESTAPI::Errors::MissingSerialNumber); // Serial/MAC are same
		}

		ProvObjects::InventoryTag inventory;
		if (!SDK::Prov::Device::Get(nullptr, device.macAddress, inventory)) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Inventory record missing for device: {}.",
									   device.macAddress));
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		if (inventory.venue.empty()) {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Inventory has no venue for device: {}.",
									   device.macAddress));
			return BadRequest(RESTAPI::Errors::VenueMustExist);
		}

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::Venue venue;
		if (!SDK::Prov::Venue::Get(nullptr, inventory.venue, venue, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().error(fmt::format("[GET-TOPOLOGY] Failed to fetch venue {} (status {}).",
										   inventory.venue, static_cast<uint32_t>(callStatus)));
				return ForwardErrorResponse(this, callStatus, callResponse);
			}
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] Failed to parse venue {} response.", inventory.venue));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (venue.boards.empty()) {
			Logger().debug(
				fmt::format("[GET-TOPOLOGY] No boards found for venue {}.", inventory.venue));
			return NotFound();
		}

		// Use the first board for topology request
		const auto &boardId = venue.boards.front();
		Poco::Net::HTTPServerResponse::HTTPStatus topoStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto topoResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Topology::Get(nullptr, boardId, topoStatus, topoResponse)) {
			if (topoStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().debug(
					fmt::format("[GET-TOPOLOGY] Failed to fetch topology for board {} (status {}).",
								boardId, static_cast<uint32_t>(topoStatus)));
				return ForwardErrorResponse(this, topoStatus, topoResponse);
			}
			Logger().debug(fmt::format(
				"[GET-TOPOLOGY] Failed to parse topology response for board {}.", boardId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		std::list<std::string> blockedMacs;
		Poco::JSON::Object::Ptr deviceObj;
		const auto gwStatus = SDK::GW::Device::GetConfig(nullptr, device.macAddress, deviceObj);
		if (gwStatus == Poco::Net::HTTPServerResponse::HTTP_OK && deviceObj && deviceObj->has("configuration")) {
			auto config = deviceObj->getObject("configuration");
			if (config) {
				SDK::GW::Device::GetBlockedClients(config, blockedMacs);
			}
		} else {
			Logger().debug(fmt::format("[GET-TOPOLOGY] Failed to fetch config for {}.", device.macAddress));
		}

		std::unordered_set<std::string> blockedMacSet;
		for (const auto &macNorm : blockedMacs) {
			blockedMacSet.insert(Utils::SerialToMAC(macNorm));
		}

		if (auto historicalDevices = topoResponse->getArray("historicalDevices")) {
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
			topoResponse->set("historicalClients", historicalClientsWithFlags);
			topoResponse->remove("historicalDevices");
		}

		if (auto nodes = topoResponse->getArray("nodes")) {
			for (std::size_t i = 0; i < nodes->size(); ++i) {
				auto node = nodes->getObject(i);
				if (!node) {
					continue;
				}
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
						if (!client || !client->has("station")) {
							continue;
						}
						std::string station;
						try {
							station = client->getValue<std::string>("station");
						} catch (...) {
							continue;
						}
						client->set("blocked", blockedMacSet.count(station) ? "1" : "0");
					}
				}
			}
		}

		return ReturnObject(*topoResponse);
	}
} // namespace OpenWifi