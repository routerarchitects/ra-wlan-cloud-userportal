/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_subscriber_devices_handler.h"
#include "framework/ow_constants.h"

#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "SubscriberCache.h"
#include "framework/utils.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {

	struct AddDeviceContext {
		std::string Mac;
		ProvObjects::InventoryTag InventoryTag{};
		SubObjects::SubscriberInfo SubscriberInfo{};
		ProvObjects::SubscriberDevice SubDevice{};
	};
	struct DeleteDeviceContext {
		std::string Mac;
		ProvObjects::InventoryTag InventoryTag{};
		SubObjects::SubscriberInfo SubscriberInfo{};
	};

	bool RESTAPI_subscriber_devices_handler::Validate_Inputs(std::string &mac) {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().error("Subscriber id is missing.");
			UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
			return false;
		}
		mac = GetBinding("mac", "");
		if (!Utils::NormalizeMac(mac)) {
			Logger().error(fmt::format("Invalid MAC: [{}]", mac));
			BadRequest(RESTAPI::Errors::InvalidMacAddress);
			return false;
		}
		return true;
	}

	bool
	RESTAPI_subscriber_devices_handler::Load_Subscriber_Info(SubObjects::SubscriberInfo &subInfo) {
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, subInfo)) {
			Logger().error(fmt::format("Failed to fetch subscriber information for id: {}.",
									   UserInfo_.userinfo.id));
			NotFound();
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::Load_Inventory_Info(
		const std::string &mac, ProvObjects::InventoryTag &inventoryTag) {
		if (!SDK::Prov::Device::Get(this, mac, inventoryTag)) {
			Logger().error(fmt::format("Inventory table has no record for MAC: {}.", mac));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		return true;
	}

	/*
		Add_Device_Validate_Subscriber:
		- If device already exists in subscriber access point list, treat as idempotent replay and
	   return OK without re-adding.
		- If inventory shows the device linked to a different subscriber, reject the request.
		- If signup shows the device linked to a different subscriber, reject the request.
		- If signup shows the device linked to this subscriber but not in SubInfoDB, allow adding
	   the device as Gateway or Mesh.
	*/
	bool RESTAPI_subscriber_devices_handler::Add_Device_Validate_Subscriber(AddDeviceContext &ctx) {

		for (const auto &ap : ctx.SubscriberInfo.accessPoints.list) {
			if (ap.macAddress == ctx.Mac) {
				Logger().error(fmt::format("Device: {} is already linked to subscriber: {}",
										   ctx.Mac, UserInfo_.userinfo.id));
				OK();
				return false;
			}
		}

		if (!ctx.InventoryTag.subscriber.empty() && ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
			Logger().information(fmt::format("Device: {} is already linked to another subscriber: {} in inventory.", ctx.Mac, ctx.InventoryTag.subscriber));
			BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
			return false;
		}

		auto signupResponse = Poco::makeShared<Poco::JSON::Object>();
		if (SDK::Prov::Signup::GetSignupDevice(nullptr, ctx.Mac, signupResponse)) {
			if (signupResponse->has("userId") &&
				signupResponse->getValue<std::string>("userId") != UserInfo_.userinfo.id) {
				Logger().error(
					fmt::format("Device: [{}] is already linked to the another subscriber [{}]",
								ctx.Mac, signupResponse->getValue<std::string>("userId")));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}
		}

		return true;
	}

	/*
		Add_Device_Gateway:
		- Create provisioning-subdevice table record for gateway device.
		- Create gateway device record in SubInfoDB.
		- Prepare default configuration for the gateway device.
		- Persist the configuration in provisioning.
		- Send the configuration to the device.
	*/
	bool RESTAPI_subscriber_devices_handler::Add_Device_Gateway(AddDeviceContext &ctx) {
		Logger().information(fmt::format("Adding a Gateway Device: {} for subscriber: {}.", ctx.Mac,
										 UserInfo_.userinfo.id));

		ConfigMaker configMaker(Logger(), ctx.SubscriberInfo.id);

		if (!SDK::Prov::Subscriber::GetSubscriberDevice(nullptr, ctx.Mac, ctx.SubDevice)) {
			if (!configMaker.CreateSubDeviceInfo(ctx.InventoryTag, UserInfo_.userinfo,
												 ctx.SubDevice)) {
				Logger().error(fmt::format(
					"Failed to build subdevice record for device: {} in owprov.", ctx.Mac));
				InternalError(RESTAPI::Errors::RecordNotCreated);
				return false;
			}
			if (!SDK::Prov::Subscriber::CreateSubsciberDevice(this, ctx.SubDevice)) {
				Logger().error(fmt::format(
					"Failed to create subdevice record for device: {} in owprov.", ctx.Mac));
				InternalError(RESTAPI::Errors::RecordNotCreated);
				return false;
			}
		}

		ProvObjects::SubscriberDeviceList devices;
		devices.subscriberDevices.push_back(ctx.SubDevice);

		StorageService()->SubInfoDB().BuildDefaultSubscriberInfo(UserInfo_, ctx.SubscriberInfo,
																 devices);

		if (!configMaker.PrepareDefaultConfig(ctx.SubscriberInfo.accessPoints.list[0],
											  ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to create default config for device: {}.", ctx.Mac));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		if (!SDK::Prov::Subscriber::UpdateSubscriberDevice(nullptr, ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to persist provisioning config for: {}.",
									   ctx.SubDevice.serialNumber));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		Logger().debug(
			fmt::format("Linking gateway device: [{}] to subscriber: [{}] in signup table.",
						ctx.Mac, UserInfo_.userinfo.id));
		if (!SDK::Prov::Signup::UpdateSignupDevice(nullptr, UserInfo_.userinfo.id, ctx.Mac)) {
			Logger().error(
				fmt::format("Failed to link device: {} to subscriber: {} in signup table.", ctx.Mac,
							UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}
		return true;
	}

	/*
		Add_Device_Mesh:
		- Fetch the gateway configuration and validate its mesh config block.
		- Create provisioning subdevice record.
		- Build the mesh configuration.
		- Prepare/persist the configuration in provisioning-subdevice database and send config to
	   device.
	*/
	bool RESTAPI_subscriber_devices_handler::Add_Device_Mesh(AddDeviceContext &ctx) {
		Logger().information(fmt::format("Adding a Mesh Device: {} for subscriber: {}.", ctx.Mac,
										 UserInfo_.userinfo.id));

		const auto gatewayMac = ctx.SubscriberInfo.accessPoints.list.front().serialNumber;
		Poco::JSON::Object::Ptr config;

		auto status = SDK::GW::Device::GetConfig(nullptr, gatewayMac, config);
		if (status != Poco::Net::HTTPResponse::HTTP_OK) {
			Logger().error(
				fmt::format("Failed to fetch gateway: {} configuration for mesh device:{}.",
							gatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}

		ConfigMaker configMaker(Logger(), ctx.SubscriberInfo.id);
		if (!configMaker.ValidateConfig(config, gatewayMac, Logger())) {
			Logger().error(fmt::format(
				"Wrong mesh configuration found on fetched gateway device:{} for mesh device: {}.",
				gatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}

		Logger().information(fmt::format("Fetched gateway configuration for: {}.", gatewayMac));

		if (!SDK::Prov::Subscriber::GetSubscriberDevice(nullptr, ctx.Mac, ctx.SubDevice)) {
			if (!configMaker.CreateSubDeviceInfo(ctx.InventoryTag, UserInfo_.userinfo,
												 ctx.SubDevice)) {
				Logger().error(fmt::format(
					"Failed to build subdevice record for device: {} in owprov.", ctx.Mac));
				InternalError(RESTAPI::Errors::RecordNotCreated);
				return false;
			}
			if (!SDK::Prov::Subscriber::CreateSubsciberDevice(this, ctx.SubDevice)) {
				Logger().error(fmt::format(
					"Failed to create subdevice record for device: {} in owprov.", ctx.Mac));
				InternalError(RESTAPI::Errors::RecordNotCreated);
				return false;
			}
		}

		Poco::JSON::Object::Ptr meshConfig;
		if (!configMaker.BuildMeshConfig(config, meshConfig)) {
			Logger().error("Failed to convert mesh configuration for provisioning.");
			InternalError(RESTAPI::Errors::ConfigurationMustExist);
			return false;
		}

		if (!configMaker.PrepareProvSubDeviceConfig(meshConfig, ctx.SubDevice.configuration)) {
			Logger().error(fmt::format(
				"Failed to store configuration for device: {} in provisioning.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		if (!SDK::Prov::Subscriber::UpdateSubscriberDevice(nullptr, ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to persist provisioning config for: {}.",
									   ctx.SubDevice.serialNumber));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}

		return true;
	}

	/*
		Add_Device_Update_Database:
		1. Link the device to the subscriber in gateway.
		2. Link the device to the subscriber in provisioning.
		3. Update SubInfoDB with new device information.
	*/
	bool RESTAPI_subscriber_devices_handler::Add_Device_Update_Database(AddDeviceContext &ctx) {

		if (!SDK::GW::Device::SetSubscriber(nullptr, ctx.Mac, UserInfo_.userinfo.id)) {
			Logger().error(fmt::format("Failed to link device: {} to subscriber: {} in gateway.",
									   ctx.Mac, UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		if (!SDK::Prov::Subscriber::SetSubscriber(nullptr, UserInfo_.userinfo.id, ctx.Mac, false)) {
			Logger().error(fmt::format("Couldn't link device: {} to subscriber: {} in inventory.",
									   ctx.Mac, UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		Logger().debug(fmt::format("Linking device: {} to subscriber: {} in owsub db.", ctx.Mac,
								   UserInfo_.userinfo.id));
		StorageService()->SubInfoDB().AddAccessPoint(ctx.SubscriberInfo, ctx.Mac,
													 ctx.InventoryTag.deviceType, ctx.SubDevice);
		if (!StorageService()->SubInfoDB().UpdateRecord("id", ctx.SubscriberInfo.id,
														ctx.SubscriberInfo)) {
			Logger().error(fmt::format(
				"Failed to update subscriber info for device: {} in SubInfoDB.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		SubscriberCache()->UpdateSubInfo(ctx.SubscriberInfo.id, ctx.SubscriberInfo);
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::Add_Device_to_Venue(AddDeviceContext &ctx,
																 bool IsGateway) {
		if (IsGateway) {
			Logger().information(fmt::format("Adding gateway device to venue for subscriber: {}.",
											 UserInfo_.userinfo.id));
			Poco::Net::HTTPServerResponse::HTTPStatus venueStatus =
				Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
			auto venueResponse = Poco::makeShared<Poco::JSON::Object>();
			if (!SDK::Prov::Subscriber::CreateSubscriberVenue(
					nullptr, UserInfo_.userinfo.id, true, std::nullopt, std::nullopt, std::nullopt,
					venueStatus, venueResponse)) {
				ForwardErrorResponse(this, venueStatus, venueResponse);
				return false;
			}
		}

		Logger().information(
			fmt::format("Adding mesh device to venue for subscriber: {}.", UserInfo_.userinfo.id));
		ProvObjects::InventoryTag gwInventoryTag;
		if (!SDK::Prov::Device::Get(
				nullptr, ctx.SubscriberInfo.accessPoints.list.front().macAddress, gwInventoryTag)) {
			Logger().error(fmt::format(
				"Inventory table has no record for MAC: {}. Unable to add device to venue.",
				ctx.SubscriberInfo.accessPoints.list.front().macAddress));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}
		if (gwInventoryTag.venue.empty()) {
			Logger().error(fmt::format(
				"Device: [{}] has no venue assigned in inventory. Cannot add device to venue.",
				ctx.SubscriberInfo.accessPoints.list.front().macAddress));
			BadRequest(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		if (!SDK::Prov::Device::UpdateInventoryVenue(nullptr, ctx.Mac, gwInventoryTag.venue,
													 ctx.InventoryTag)) {
			Logger().error(fmt::format("Failed to add device: [{}] to venue: [{}] in provisioning.",
									   ctx.SubscriberInfo.accessPoints.list.front().macAddress,
									   gwInventoryTag.venue));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}
		return true;
	}

	/*
	DoPost():
	- Validate subscriber-ID and MAC address format.
	- Verify MAC exists in inventory and isn't already provisioned to another/same subscriber.
	- Load subscriber information from database.
	- Setup device based on subscriber's existing device record:
		- First device: Configure as Gateway device with default config.
		- Additional devices: Configure as Mesh node using gateway's mesh settings.
	- Update device record in Gateway, Provisioning, SubInfo database.
	- Return OK on success or appropriate error response on failure.
	*/
	void RESTAPI_subscriber_devices_handler::DoPost() {
		AddDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac))
			return;

		Logger().information(
			fmt::format("Processing Add Device: [{}] request for subscriber: [{}].", ctx.Mac,
						UserInfo_.userinfo.id));

		if (!Load_Subscriber_Info(ctx.SubscriberInfo))
			return;

		if (!Load_Inventory_Info(ctx.Mac, ctx.InventoryTag))
			return;

		if (!Add_Device_Validate_Subscriber(ctx))
			return;

		if (ctx.SubscriberInfo.accessPoints.list.empty()) {
			if (!Add_Device_Gateway(ctx))
				return;
			if (!Add_Device_to_Venue(ctx, true))
				return;
		} else {
			if (!Add_Device_Mesh(ctx))
				return;
			if (!Add_Device_to_Venue(ctx, false))
				return;
		}

		if (!Add_Device_Update_Database(ctx))
			return;

		return OK();
	}

	/*
		Delete_Device_Validate_Subscriber:
		- Check inventory: if another subscriber owns it, reject; if this subscriber owns it,
	   allow deletion.
		- If inventory shows no owner, but device still exists in subscriber's database, allow
	   deletion.
		- Otherwise return DeviceNotFound.
	*/
	bool RESTAPI_subscriber_devices_handler::Delete_Device_Validate_Subscriber(
		DeleteDeviceContext &ctx) {

		if (!ctx.InventoryTag.subscriber.empty()) {
			if (ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(
					fmt::format("Subscriber [{}] trying to delete device [{}] owned by [{}].",
								UserInfo_.userinfo.id, ctx.Mac, ctx.InventoryTag.subscriber));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}

			// If owsub-DB contains the device under this subscriber, Allow deletion.
			for (const auto &ap : ctx.SubscriberInfo.accessPoints.list) {
				if (ap.macAddress == ctx.Mac) {
					Logger().information(fmt::format(
						"Subscriber [{}] verified as owner of device [{}], allowing deletion.",
						UserInfo_.userinfo.id, ctx.Mac));
					return true;
				}
			}
		}
		Logger().warning(fmt::format("Device: [{}] not found for subscriber: [{}].", ctx.Mac,
								   UserInfo_.userinfo.id));
		OK(); // idempotent response
		return false;
	}

	/*
		Delete_Device_Update_Database:
		- Send factory reset command to device.
		- Delete device record from controller (owgw-devicesDB).
		- Delete provisioning subdevice record (owprov-sub_deviceDB).
		- Clear signup mac/serial if present.
		- Delete inventory record (owprov-inventoryDB).
	*/
	bool
	RESTAPI_subscriber_devices_handler::Delete_Device_Update_Database(DeleteDeviceContext &ctx) {
		Logger().information(
			fmt::format("Sending factory reset command to device [{}] and deleting "
						"records from gateway, provisioning and subscriber.",
						ctx.Mac));
		SDK::GW::Device::Factory(nullptr, ctx.Mac, 0, true);
		if (ctx.SubscriberInfo.accessPoints.list.front().macAddress == ctx.Mac) {
			Delete_Device_from_Venue(ctx);
			SDK::Prov::Signup::UpdateSignupDevice(nullptr, UserInfo_.userinfo.id, "");
		}
		SDK::GW::Device::DeleteOwgwDevice(this, ctx.Mac);
		SDK::Prov::Subscriber::DeleteSubscriberDevice(nullptr, ctx.Mac);
		SDK::Prov::Device::DeleteInventoryDevice(nullptr, ctx.Mac);
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::Delete_Device_from_Venue(DeleteDeviceContext &ctx) {
		Logger().information(
			fmt::format("Deleting venue for subscriber: [{}].", ctx.SubscriberInfo.id));
		Poco::Net::HTTPServerResponse::HTTPStatus venueStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		if (!SDK::Prov::Subscriber::DeleteSubscriberVenue(nullptr, UserInfo_.userinfo.id,
														  venueStatus)) {
			ForwardErrorResponse(this, venueStatus, Poco::makeShared<Poco::JSON::Object>());
			return false;
		}
		return true;
	}

	/*
		DoDelete():
		- Validate subscriber-ID and MAC format, then load subscriber info.
		- Ensure the subscriber owns the device (inventory/subinfo checks).
		- If deleting the gateway device, delete all devices for this subscriber and clear the
	   signup MAC.
		- If deleting a mesh device, remove only that device from databases.
	*/
	void RESTAPI_subscriber_devices_handler::DoDelete() {

		DeleteDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac))
			return;

		Logger().information(
			fmt::format("Processing Delete Device [{}] request for subscriber: [{}].", ctx.Mac,
						UserInfo_.userinfo.id));

		if (!Load_Subscriber_Info(ctx.SubscriberInfo))
			return;

		if (!Load_Inventory_Info(ctx.Mac, ctx.InventoryTag))
			return;

		if (!Delete_Device_Validate_Subscriber(ctx))
			return;

		auto &apList = ctx.SubscriberInfo.accessPoints.list;
		if (apList.front().macAddress == ctx.Mac) {
			Logger().debug(fmt::format("Deleting all devices present under subscriber: [{}].",
									   ctx.SubscriberInfo.id));
			for (const auto &ap : apList) {
				ctx.Mac = ap.macAddress;
				if (!Delete_Device_Update_Database(ctx))
					return;
			}
			apList.clear(); // deleting gateway -> clear all
		} else {
			SubObjects::AccessPointList updated;
			for (const auto &ap : apList) {
				if (ap.macAddress != ctx.Mac)
					updated.list.push_back(ap);
			}
			Logger().debug(fmt::format("Deleting mesh device: [{}] for subscriber: [{}].", ctx.Mac,
									   ctx.SubscriberInfo.id));
			apList = updated.list; // deleting mesh -> remove only that device
			if (!Delete_Device_Update_Database(ctx))
				return;
		}
		StorageService()->SubInfoDB().UpdateRecord("id", ctx.SubscriberInfo.id, ctx.SubscriberInfo);
		SubscriberCache()->UpdateSubInfo(ctx.SubscriberInfo.id, ctx.SubscriberInfo);
		return OK();
	}
} // namespace OpenWifi
