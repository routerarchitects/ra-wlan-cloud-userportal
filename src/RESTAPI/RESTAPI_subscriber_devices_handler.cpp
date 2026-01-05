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

	bool RESTAPI_subscriber_devices_handler::Get_Signup_Record(const std::string &email, ProvObjects::SignupEntry &SE) {
		if (!SDK::Prov::Signup::Get_Signup_By_Email(this, email, SE)) {
			Logger().error(fmt::format("Failed to fetch signup record for email: {}.", email));
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::Update_Prov_Signup_DB(const std::string &email, const std::string &mac) {
		ProvObjects::SignupEntry SE;
		if (!Get_Signup_Record(email, SE)) {
			return false;
		}
		return SDK::Prov::Signup::Update_Signup_Mac(this, SE.info.id, mac);
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
        Add_Device_Validate_Subscriber:
        1. Ensure inventory has a record for the provided MAC.
        2. Ensure the device is not already provisioned to another subscriber.
        3. If already linked to this subscriber, check if device exists in SubInfoDB and return error if found.
        4. If linked to same subcriber but not in SubInfoDB, proceed with adding the device as Gateway or Mesh.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Validate_Subscriber(AddDeviceContext &ctx) {

		if (!SDK::Prov::Device::Get(this, ctx.Mac, ctx.InventoryTag)) {
			Logger().error(fmt::format("Inventory table has no record for MAC: {}.", ctx.Mac));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}

		if (!ctx.InventoryTag.subscriber.empty()) {
			if (ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(fmt::format("Device: {} is already provisioned to subscriber: {}", ctx.Mac, ctx.InventoryTag.subscriber));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}

			// If owsub-DB contain the device under this subscriber, Do not allow re-provisioning.
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
        Add_Device_Gateway:
        1. Create provisioning-subdevice table record for gateway device.
        2. Create gateway device record in SubInfoDB.
        2. Prepare default configuration for the gateway device.
        3. Persist the configuration in provisioning.
        4. Send the configuration to the device.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Gateway(AddDeviceContext &ctx) {
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
        Add_Device_Mesh:
        1. Ensure there is an existing gateway device for the subscriber.
        1. Fetch the gateway configuration and validate its mesh config block.
        3. Create provisioning subdevice record.
        4. Build the mesh configuration.
        5. Prepare/persist the configuration in provisioning-subdevice database and send config to device.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Mesh(AddDeviceContext &ctx) {
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
        Add_Device_Update_Database:
        1. Link the device to the subscriber in gateway.
        2. Link the device to the subscriber in provisioning.
        3. Update SubInfoDB with new device information.
    */
	bool RESTAPI_subscriber_devices_handler::Add_Device_Update_Database(AddDeviceContext &ctx) {

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
		if ((ctx.SubscriberInfo.accessPoints.list.front().macAddress == ctx.Mac)) {
			Logger().information(fmt::format("Linking gateway device: [{}] to subscriber: [{}] in signup table.", ctx.Mac, UserInfo_.userinfo.email));
			if (!Update_Prov_Signup_DB(UserInfo_.userinfo.email, ctx.Mac)) {
				Logger().error(fmt::format("Failed to link device: {} to subscriber: {} in signup table.", ctx.Mac, UserInfo_.userinfo.email));
				InternalError(RESTAPI::Errors::RecordNotUpdated);
				return false;
			}
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

		AddDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac)) return;

		Logger().information(fmt::format("Processing Add Device: [{}] request for subscriber: [{}].", ctx.Mac, UserInfo_.userinfo.id));

		if (!Load_Subscriber_Info(ctx.SubscriberInfo)) return;

		if (!Add_Device_Validate_Subscriber(ctx)) return;

		if (ctx.SubscriberInfo.accessPoints.list.empty()) {
			if (!Add_Device_Gateway(ctx)) return;
		} else {
			if (!Add_Device_Mesh(ctx)) return;
		}

		if (!Add_Device_Update_Database(ctx)) return;

		return OK();
	}

	struct DeleteDeviceContext {
		std::string Mac;
		ProvObjects::InventoryTag InventoryTag{};
		SubObjects::SubscriberInfo SubscriberInfo{};
	};

	/*
		Delete_Device_Validate_Subscriber:
		- Check inventory: if another subscriber owns it, reject; if this subscriber owns it, allow deletion.
		- If inventory shows no owner, but device still exists in subscriber's database, allow deletion.
		- Otherwise return DeviceNotFound.
	*/
	bool RESTAPI_subscriber_devices_handler::Delete_Device_Validate_Subscriber(DeleteDeviceContext &ctx) {

		if (!SDK::Prov::Device::Get(this, ctx.Mac, ctx.InventoryTag)) {
			Logger().error(fmt::format("Inventory table has no record for MAC: {}.", ctx.Mac));
			BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
			return false;
		}

		if (!ctx.InventoryTag.subscriber.empty()) {
			if (ctx.InventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(fmt::format("Subscriber [{}] trying to delete device [{}] owned by [{}].", UserInfo_.userinfo.id, ctx.Mac, ctx.InventoryTag.subscriber));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return false;
			}

			// If owsub-DB contains the device under this subscriber, Allow deletion.
			for (const auto &ap : ctx.SubscriberInfo.accessPoints.list) {
				if (ap.macAddress == ctx.Mac)  {
					Logger().information(fmt::format("Subscriber [{}] verified as owner of device [{}], allowing deletion.", UserInfo_.userinfo.id, ctx.Mac));
					return true;
				}
			}
		}
		Logger().error(fmt::format("Device: [{}] not found for subscriber: [{}].", ctx.Mac, UserInfo_.userinfo.id));
		BadRequest(RESTAPI::Errors::RecordNotFound);
		return false;
	}

	/*
		Delete_Device_Update_Database:
		1) Send factory reset command to device.
		2) Delete device record from controller (owgw-devicesDB).
		3) Delete provisioning subdevice record (owprov-sub_deviceDB).
		4) Clear signup mac/serial if present.
		5) Delete inventory record (owprov-inventoryDB).
	*/
	bool RESTAPI_subscriber_devices_handler::Delete_Device_Update_Database(DeleteDeviceContext &ctx) {
		Logger().information(fmt::format("Sending factory reset command to device [{}] and deleting "
			"records from gateway, provisioning and subscriber.", ctx.Mac));
		SDK::GW::Device::Factory(nullptr, ctx.Mac, 0, true);
		if (ctx.SubscriberInfo.accessPoints.list.front().macAddress == ctx.Mac) {
			Update_Prov_Signup_DB(UserInfo_.userinfo.email, "");
		}
		SDK::GW::Device::DeleteOwgwDevice(this, ctx.Mac);
		SDK::Prov::Subscriber::DeleteProvSubscriberDevice(this, ctx.Mac);
		SDK::Prov::Device::DeleteInventoryDevice(this, ctx.Mac);
		return true;
	}

	void RESTAPI_subscriber_devices_handler::DoDelete() {

		DeleteDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac)) return;

		Logger().information(fmt::format("Processing Delete Device [{}] request for subscriber: [{}].", ctx.Mac, UserInfo_.userinfo.id));

		if (!Load_Subscriber_Info(ctx.SubscriberInfo)) return;
		if (!Delete_Device_Validate_Subscriber(ctx)) return;

		auto &apList = ctx.SubscriberInfo.accessPoints.list;
		if (apList.front().macAddress == ctx.Mac) {
			Logger().information(fmt::format("Deleting all devices present under subscriber: [{}].", ctx.SubscriberInfo.id));
			for (const auto &ap : apList) {
				ctx.Mac = ap.macAddress;
				if (!Delete_Device_Update_Database(ctx)) return;
			}
			apList.clear();  // deleting gateway -> clear all
		} else {
			SubObjects::AccessPointList updated;
			for (const auto &ap : apList) {
				if (ap.macAddress != ctx.Mac)
					updated.list.push_back(ap);
			}
			Logger().information(fmt::format("Deleting mesh device: [{}] for subscriber: [{}].", ctx.Mac, ctx.SubscriberInfo.id));
			apList = updated.list; // deleting mesh -> remove only that device
			if (!Delete_Device_Update_Database(ctx)) return;
		}
		if (!StorageService()->SubInfoDB().UpdateRecord("id", ctx.SubscriberInfo.id, ctx.SubscriberInfo)) return;
		return OK();
	}
} // namespace OpenWifi
