/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

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
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
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

		return ReturnObject(*topologyResponse);
	}
} // namespace OpenWifi
