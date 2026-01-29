/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_topology_handler.h"

#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/ow_constants.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_nw_topology.h"

namespace OpenWifi {

	void RESTAPI_topology_handler::DoGet() {
		if (UserInfo_.userinfo.id.empty()) {
            Logger().warning("Received topology request without subscriber id.");
			return NotFound();
		}

		SubObjects::SubscriberInfo subscriberInfo;
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, subscriberInfo)) {
            Logger().warning(fmt::format("Subscriber {} not found for topology request.", UserInfo_.userinfo.id));
			return NotFound();
		}

		if (subscriberInfo.accessPoints.list.empty()) {
            Logger().warning(fmt::format("Subscriber {} has no access points for topology request.", UserInfo_.userinfo.id));
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		const auto &device = subscriberInfo.accessPoints.list.front();
		if (device.macAddress.empty()) {
            Logger().warning(fmt::format("Subscriber {} access point has no MAC address for topology request.", UserInfo_.userinfo.id));
			return BadRequest(RESTAPI::Errors::MissingSerialNumber);
		}

		ProvObjects::InventoryTag inventory;
		if (!SDK::Prov::Device::Get(nullptr, device.macAddress, inventory)) {
			Logger().error(fmt::format("Inventory record missing for device: {}.", device.macAddress));
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		if (inventory.venue.empty()) {
			Logger().warning(fmt::format("Inventory has no venue for device: {}.", device.macAddress));
			return BadRequest(RESTAPI::Errors::VenueMustExist);
		}

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto callResponse = Poco::makeShared<Poco::JSON::Object>();
		ProvObjects::Venue venue;
		if (!SDK::Prov::Venue::Get(nullptr, inventory.venue, venue, callStatus, callResponse)) {
			if (callStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().error(fmt::format("Failed to fetch venue {} (status {}).", inventory.venue,
										   static_cast<uint32_t>(callStatus)));
				return ForwardErrorResponse(this, callStatus, callResponse);
			}
			Logger().error(fmt::format("Failed to parse venue {} response.", inventory.venue));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		if (venue.boards.empty()) {
			Logger().warning(fmt::format("No boards found for venue {}.", inventory.venue));
			return BadRequest(RESTAPI::Errors::RecordNotFound);
		}

		const auto &boardId = venue.boards.front();
		Poco::Net::HTTPServerResponse::HTTPStatus topoStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto topoResponse = Poco::makeShared<Poco::JSON::Object>();
		if (!SDK::Topology::Get(nullptr, boardId, topoStatus, topoResponse)) {
			if (topoStatus != Poco::Net::HTTPServerResponse::HTTP_OK) {
				Logger().error(fmt::format("Failed to fetch topology for board {} (status {}).",
										   boardId, static_cast<uint32_t>(topoStatus)));
				return ForwardErrorResponse(this, topoStatus, topoResponse);
			}
			Logger().error(fmt::format("Failed to parse topology response for board {}.", boardId));
			return InternalError(RESTAPI::Errors::InternalError);
		}

		return ReturnObject(*topoResponse);
	}

} // namespace OpenWifi
