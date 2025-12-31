/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "framework/ow_constants.h"
#include "RESTAPI_subscriber_devices_handler.h"

#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "SubscriberCache.h"
#include "framework/utils.h"
#include "nlohmann/json.hpp"
#include "sdks/SDK_fms.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_sec.h"

namespace OpenWifi {

	bool RESTAPI_subscriber_devices_handler::Validate_Inputs(std::string &mac) {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().error("Subscriber id is missing.");
			NotFound();
			return false;
		}
		mac = GetBinding("mac", "");
		if (!Utils::NormalizeMac(mac)) {
			Logger().error(fmt::format("Invalid MAC: [{}]", mac));
			BadRequest(RESTAPI::Errors::InvalidSerialNumber);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::Load_Subscriber_Info(SubObjects::SubscriberInfo &subInfo) {
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, subInfo)) {
			Logger().error(fmt::format("Failed to fetch subscriber information for id: {}.", UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotFound);
			return false;
		}
		return true;
	}

	struct AddDeviceContext {
		std::string Mac;
		ProvObjects::InventoryTag InventoryTag{};
		SubObjects::SubscriberInfo SubscriberInfo{};
		Poco::JSON::Object::Ptr GatewayConfig{nullptr};
		ProvObjects::SubscriberDevice SubDevice{};
		ProvObjects::InventoryConfigApplyResult ApplyResult{};
	};

    /*
        Add_Device_Validate_Ownership:
        1. Ensure inventory has a record for the provided MAC.
        2. Ensure the device is not already provisioned to another subscriber.
        3. If already linked to this subscriber, check if device exists in SubInfoDB and return error if found.
        4. If linked to same subcriber but not in SubInfoDB, proceed with adding the device as Gateway or Mesh.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Validate_Ownership(AddDeviceContext &ctx) {

		if (!SDK::Prov::Device::Get(this, ctx.Mac, ctx.InventoryTag)) {
			Logger().error(fmt::format("Inventory table has no record for MAC: {}.", ctx.Mac));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}

		StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, ctx.SubscriberInfo);
		if (!ctx.InventoryTag.subscriber.empty()) {
			if (ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(fmt::format("Device: {} is already provisioned to subscriber: {}", ctx.Mac, ctx.InventoryTag.subscriber));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}
			for (const auto &ap : ctx.SubscriberInfo.accessPoints.list) {
				if (ap.macAddress == ctx.Mac) {
					Logger().error(fmt::format("Device: {} is already linked to subscriber: {}", ctx.Mac, UserInfo_.userinfo.id));
					BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
					return false;
				}
			}
		}
		return true;
	}

    /*
        Add_Device_Setup_Gateway:
        1. Create provisioning-subdevice table record for gateway device.
        2. Create gateway device record in SubInfoDB.
        2. Prepare default configuration for the gateway device.
        3. Persist the configuration in provisioning.
        4. Send the configuration to the device.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Setup_Gateway(AddDeviceContext &ctx) {
        Logger().information(fmt::format("Adding a Gateway Device: {} for subscriber: {}.", ctx.Mac, UserInfo_.userinfo.id));

		ProvObjects::SubscriberDevice dev;
		if (!SDK::Prov::Subscriber::CreateSubDeviceInfo(this, ctx.InventoryTag, UserInfo_.userinfo, dev)) {
			Logger().error(fmt::format("Failed to create subdevice record for device: {} in owprov.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotCreated);
			return false;
		}

		ProvObjects::SubscriberDeviceList devices;
		devices.subscriberDevices.push_back(dev);

		StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, ctx.SubscriberInfo, devices);

		ConfigMaker InitialConfig(Logger(), ctx.SubscriberInfo.id);
		if (!SDK::Prov::Subscriber::GetDevice(this, ctx.SubscriberInfo.accessPoints.list[0].macAddress, ctx.SubDevice)) {
			Logger().error(fmt::format("Could not find provisioning subdevice for: {}.", ctx.Mac));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		if (!InitialConfig.PrepareDefaultConfig(ctx.SubscriberInfo.accessPoints.list[0], ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to create default config for device: {}.", ctx.Mac));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		if (!SDK::Prov::Subscriber::SetDevice(this, ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to persist provisioning config for: {}.", ctx.SubDevice.serialNumber));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		return true;
	}

    /*
        Add_Device_Setup_Mesh:
        1. Ensure there is an existing gateway device for the subscriber.
        1. Fetch the gateway configuration and validate its mesh config block.
        3. Create provisioning subdevice record.
        4. Build the mesh configuration.
        5. Prepare/persist the configuration in provisioning-subdevice database and send config to device.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Setup_Mesh(AddDeviceContext &ctx) {
        Logger().information(fmt::format("Adding a Mesh Device: {} for subscriber: {}.", ctx.Mac, UserInfo_.userinfo.id));

		if (ctx.SubscriberInfo.accessPoints.list.empty()) {
			Logger().error("Mesh setup requested but no existing gateway device found.");
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}

		const auto gatewayMac = ctx.SubscriberInfo.accessPoints.list.front().serialNumber;
		Poco::JSON::Object::Ptr config;

		auto status = SDK::GW::Device::GetConfig(this, gatewayMac, config);
		if (status != Poco::Net::HTTPResponse::HTTP_OK) {
			Logger().error(fmt::format("Failed to fetch gateway: {} configuration for mesh device:{}.", gatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}

		if (!SDK::GW::Device::ValidateMeshSSID(config, gatewayMac, Logger())) {
			Logger().error(fmt::format("Wrong mesh configuration found on fetched gateway device:{} for mesh device: {}.", gatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}

		ctx.GatewayConfig = config->getObject("configuration");
		Logger().information(fmt::format("Fetched gateway configuration for: {}.", gatewayMac));

		if (!SDK::Prov::Subscriber::CreateSubDeviceInfo(this, ctx.InventoryTag, UserInfo_.userinfo, ctx.SubDevice)) {
			Logger().error(fmt::format("Failed to create subdevice record for device: {} in owprov.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotCreated);
			return false;
		}

		auto meshConfig = SDK::Prov::Subscriber::BuildMeshConfig(ctx.GatewayConfig);
		if (!meshConfig) {
			Logger().error("Failed to convert mesh configuration for provisioning.");
			InternalError(RESTAPI::Errors::ConfigurationMustExist);
			return false;
		}

		ConfigMaker MeshConfig(Logger(), ctx.SubscriberInfo.id);
		if (!MeshConfig.PrepareProvSubDeviceConfig(meshConfig, ctx.SubDevice.configuration)) {
			Logger().error(fmt::format("Failed to store configuration for device: {} in provisioning.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		SDK::Prov::Subscriber::SetDevice(this, ctx.SubDevice);

		return true;
	}

    /*
        Add_Device_Update_Db:
        1. Link the device to the subscriber in gateway.
        2. Link the device to the subscriber in provisioning.
        3. Update SubInfoDB with new device information.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Update_Db(AddDeviceContext &ctx) {

		if (ctx.SubDevice.serialNumber.empty()) {
			Logger().error(fmt::format("Provisioned device information is missing for device {}.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		if (!SDK::GW::Device::SetSubscriber(this, ctx.Mac, UserInfo_.userinfo.id)) {
			Logger().error(fmt::format("Failed to link device: {} to subscriber: {} in gateway.", ctx.Mac, UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		if (!SDK::Prov::Subscriber::UpdateSubscriber(this, UserInfo_.userinfo.id, ctx.Mac, false)) {
			Logger().error(fmt::format("Couldn't link device: {} to subscriber: {} in inventory.", ctx.Mac, UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}

		Logger().information(fmt::format("Linking device: {} to subscriber: {} in owsub db.", ctx.Mac, UserInfo_.userinfo.id));
		StorageService()->SubInfoDB().AddAccessPoint(ctx.SubscriberInfo, ctx.Mac, ctx.InventoryTag.deviceType, ctx.SubDevice);
		if (!StorageService()->SubInfoDB().UpdateRecord("id", ctx.SubscriberInfo.id, ctx.SubscriberInfo)) {
			Logger().error(fmt::format("Failed to update subscriber info for device: {} in SubInfoDB.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}
		return true;
	}

    /*
        DoPost():
        1. Validate subscriber-ID and MAC address format.
        2. Verify MAC exists in inventory and isn't already provisioned to another/same subscriber.
        3. Load subscriber information from database.
        4. Setup device based on subscriber's existing device record:
        - First device: Configure as Gateway device with default config.
        - Additional devices: Configure as Mesh node using gateway's mesh settings.
        5. Update device record in Gateway, Provisioning, SubInfo database.
        6. Return OK on success or appropriate error response on failure.
    */
	void RESTAPI_subscriber_devices_handler::DoPost() {
		Logger().information(fmt::format("Processing Add Device request for subscriber: {}.", UserInfo_.userinfo.id));

		AddDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac)) return;
		if (!Add_Device_Validate_Ownership(ctx)) return;
		if (!Load_Subscriber_Info(ctx.SubscriberInfo)) return;

		if (ctx.SubscriberInfo.accessPoints.list.empty()) {
			if (!Add_Device_Setup_Gateway(ctx)) return;
		} else {
			if (!Add_Device_Setup_Mesh(ctx)) return;
		}

		if (!Add_Device_Update_Db(ctx)) return;

		return OK();
	}

	struct DeleteDeviceContext {
		std::string RemoveMac{};
		ProvObjects::InventoryTag InventoryTag{};
		SubObjects::SubscriberInfo SubscriberInfo{};
	};

	/*
		Delete_Device_Validate_Ownership:
		- Check inventory: if another subscriber owns it, reject; if this subscriber owns it, allow deletion.
		- If inventory shows no owner, but device still exists in subscriber's database, allow deletion.
		- Otherwise return DeviceNotFound.
	*/
	bool RESTAPI_subscriber_devices_handler::Delete_Device_Validate_Ownership(DeleteDeviceContext &ctx) {

		if (SDK::Prov::Device::Get(this, ctx.RemoveMac, ctx.InventoryTag)) {

			if (!ctx.InventoryTag.subscriber.empty() && ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(fmt::format("Subscriber [{}] trying to delete device [{}] owned by [{}].", UserInfo_.userinfo.id, ctx.RemoveMac, ctx.InventoryTag.subscriber));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}
			if (ctx.InventoryTag.subscriber == UserInfo_.userinfo.id) {
				Logger().information(fmt::format("Subscriber [{}] verified as owner of device [{}], allowing deletion.", UserInfo_.userinfo.id, ctx.RemoveMac));
				return true;
			}
		}
		// If owsub-DB contains the device under this subscriber, allow delete.
		if (StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, ctx.SubscriberInfo)) {
			for (const auto &ap : ctx.SubscriberInfo.accessPoints.list) {
				if (ap.macAddress == ctx.RemoveMac) {
					Logger().information(fmt::format("Subscriber [{}] verified as owner of device [{}], allowing deletion.", UserInfo_.userinfo.id, ctx.RemoveMac));
					return true;
				}
			}
		}
		Logger().error(fmt::format("No device found with MAC [{}] for subscriber [{}].", ctx.RemoveMac, UserInfo_.userinfo.id));
		NotFound();
		return false;
	}

	/*
		Execute_Gateway_Delete:
		1) For each device in subscriber's device list:
		   - Send factory reset command.
		   - Delete device record from controller (owgw-devicesDB).
		   - Delete provisioning subdevice record (owprov-sub_deviceDB).
		   - Delete inventory record (owprov-inventoryDB).
		2) Clear subscriber's device list in subscriber own database (owsub-SubInfoDB).
	*/
		bool RESTAPI_subscriber_devices_handler::Execute_Gateway_Delete(DeleteDeviceContext &ctx) {
		Logger().information(fmt::format("Deleting all devices for subscriber [{}].", UserInfo_.userinfo.id));

    	auto &SI = ctx.SubscriberInfo;
		for (const auto &ap : SI.accessPoints.list) {
			if (!Delete_Device_From_All_Databases(ap.macAddress)) {
				InternalError(RESTAPI::Errors::RecordNotDeleted);
				return false;
			}
		}

		SI.accessPoints.list.clear();
		if (!StorageService()->SubInfoDB().UpdateRecord("id", UserInfo_.userinfo.id, SI)) {
			Logger().error(fmt::format("Failed to clear owsub accessPoints for subscriber [{}].", UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotUpdated);
			return false;
		}
		return true;
	}

	/*
		Execute_Mesh_Delete:
		1) For single mesh device:
		   - Send factory reset command to that device.
		   - Delete device record from controller (owgw-devicesDB).
		   - Delete provisioning subdevice record (owprov-sub_deviceDB).
		   - Delete inventory record (owprov-inventoryDB).
		   - Delete single device data from susbcriber's own database (owsub-SubInfoDB).
	*/
	bool RESTAPI_subscriber_devices_handler::Execute_Mesh_Delete(DeleteDeviceContext &ctx) {
		Logger().information(fmt::format("Deleting mesh device [{}] for subscriber [{}].", ctx.RemoveMac, UserInfo_.userinfo.id));

		auto &SI = ctx.SubscriberInfo;
		if (!Delete_Device_From_All_Databases(ctx.RemoveMac)) {
			InternalError(RESTAPI::Errors::RecordNotDeleted);
			return false;
		}

			// Drop this MAC data from accessPoints and update the subscriber record
			SubObjects::AccessPointList updatedList;
			for (const auto &ap : SI.accessPoints.list) {
				if (ap.macAddress != ctx.RemoveMac) {
					updatedList.list.push_back(ap);
				}
			}
			SI.accessPoints.list = updatedList.list;

			if (!StorageService()->SubInfoDB().UpdateRecord("id", UserInfo_.userinfo.id, SI)) {
				Logger().error(fmt::format("Failed to update owsub record for subscriber [{}].", UserInfo_.userinfo.id));
				InternalError(RESTAPI::Errors::RecordNotUpdated);
				return false;
			}
		return true;
	}

	/*
		Delete_Device_From_All_Databases:
		1) Send factory reset command to device.
		2) Delete device record from controller (owgw-devicesDB).
		3) Delete provisioning subdevice record (owprov-sub_deviceDB).
		4) Delete inventory record (owprov-inventoryDB).
	*/
	bool RESTAPI_subscriber_devices_handler::Delete_Device_From_All_Databases(const std::string &mac) {

		Logger().information(fmt::format("Sending factory reset command to device [{}]", mac));
		SDK::GW::Device::Factory(nullptr, mac, 0, true);

		if (!SDK::GW::Device::DeleteOwgwDevice(this, mac)) {
			Logger().error(fmt::format("Controller delete failed (or already deleted) for [{}].", mac));
			return false;
		}

		if (!SDK::Prov::Subscriber::DeleteProvSubscriberDevice(this, mac)) {
			Logger().error(fmt::format("Provisioning subdevice delete failed (or already deleted) for [{}].", mac));
			return false;
		}

		if (!SDK::Prov::Device::DeleteInventoryDevice(this, mac)) {
			Logger().error(fmt::format("Inventory delete failed (or already deleted) for [{}].", mac));
			return false;
		}
		return true;
	}

	void RESTAPI_subscriber_devices_handler::DoDelete() {
		DeleteDeviceContext ctx;

		if (!Validate_Inputs(ctx.RemoveMac))
			return;

		if (!Delete_Device_Validate_Ownership(ctx))
			return;

		if (!Load_Subscriber_Info(ctx.SubscriberInfo))
			return;

		if (!ctx.SubscriberInfo.accessPoints.list.empty() && ctx.SubscriberInfo.accessPoints.list.front().macAddress == ctx.RemoveMac) {
			if (!Execute_Gateway_Delete(ctx))
				return;
		} else {
			if (!Execute_Mesh_Delete(ctx))
				return;
		}
		return OK();
	}
} // namespace OpenWifi
