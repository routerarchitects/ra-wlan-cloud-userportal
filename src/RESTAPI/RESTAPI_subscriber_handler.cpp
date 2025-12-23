/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-11-07.
//
#include "framework/ow_constants.h"

#include "RESTAPI_subscriber_handler.h"
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

	template <typename T> void AssignIfModified(T &Var, const T &Value, int &Mods) {
		if (Var != Value) {
			Var = Value;
			Mods++;
		}
	}
/*
	GET /api/v1/subscriber on a first call:
	1. Fetch provisioning subdevice record + device current config from controller.
	2. Build default SSID/password and provisioning config from the controller data.
	3. Persist the provisioning subdevice record, link the device to the subscriber (gateway + inventory), then store the subscriber record in the database.
*/
	void RESTAPI_subscriber_handler::DoGet() {

		if (UserInfo_.userinfo.id.empty()) {
			return NotFound();
		}

		Logger().information(fmt::format("{}: Get basic info.", UserInfo_.userinfo.email));
		SubObjects::SubscriberInfo SI;
		if (StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI)) {

			int Mods = 0;

			//  we need to get the stats for each AP
			for (auto &i : SI.accessPoints.list) {
				if (i.macAddress.empty())
					continue;
				Poco::JSON::Object::Ptr LastStats;
				if (SDK::GW::Device::GetLastStats(nullptr, i.serialNumber, LastStats)) {
					std::ostringstream OS;
					LastStats->stringify(OS);
					try {
						nlohmann::json LA = nlohmann::json::parse(OS.str());
						for (const auto &j : LA["interfaces"]) {
							if (j.contains("location") &&
								j["location"].get<std::string>() == "/interfaces/0" &&
								j.contains("ipv4")) {

								if (j["ipv4"]["addresses"].is_array() &&
									!j["ipv4"]["addresses"].empty()) {
									auto IPParts = Poco::StringTokenizer(
										j["ipv4"]["addresses"][0].get<std::string>(), "/");
									AssignIfModified(i.internetConnection.ipAddress, IPParts[0],
													 Mods);
									i.internetConnection.subnetMask = IPParts[1];
								}
								if (j["ipv4"].contains("dhcp_server"))
									AssignIfModified(i.internetConnection.defaultGateway,
													 j["ipv4"]["dhcp_server"].get<std::string>(),
													 Mods);
								else
									AssignIfModified(i.internetConnection.defaultGateway,
													 std::string{"---"}, Mods);

								if (j.contains("dns_servers") && j["dns_servers"].is_array()) {
									auto dns = j["dns_servers"];
									if (!dns.empty())
										AssignIfModified(i.internetConnection.primaryDns,
														 dns[0].get<std::string>(), Mods);
									else
										AssignIfModified(i.internetConnection.primaryDns,
														 std::string{"---"}, Mods);
									if (dns.size() > 1)
										AssignIfModified(i.internetConnection.secondaryDns,
														 dns[1].get<std::string>(), Mods);
									else
										AssignIfModified(i.internetConnection.secondaryDns,
														 std::string{"---"}, Mods);
								}
							}
						}
					} catch (...) {
						AssignIfModified(i.internetConnection.ipAddress, std::string{"--"}, Mods);
						i.internetConnection.subnetMask = "--";
						i.internetConnection.defaultGateway = "--";
						i.internetConnection.primaryDns = "--";
						i.internetConnection.secondaryDns = "--";
					}
				} else {
					AssignIfModified(i.internetConnection.ipAddress, std::string{"-"}, Mods);
					i.internetConnection.subnetMask = "-";
					i.internetConnection.defaultGateway = "-";
					i.internetConnection.primaryDns = "-";
					i.internetConnection.secondaryDns = "-";
				}

				FMSObjects::DeviceInformation DI;
				if (SDK::FMS::Firmware::GetDeviceInformation(nullptr, i.serialNumber, DI)) {
					AssignIfModified(i.currentFirmwareDate, DI.currentFirmwareDate, Mods);
					AssignIfModified(i.currentFirmware, DI.currentFirmware, Mods);
					AssignIfModified(i.latestFirmwareDate, DI.latestFirmwareDate, Mods);
					AssignIfModified(i.latestFirmware, DI.latestFirmware, Mods);
					AssignIfModified(i.newFirmwareAvailable, DI.latestFirmwareAvailable, Mods);
					AssignIfModified(i.latestFirmwareURI, DI.latestFirmwareURI, Mods);
				}
			}

			if (Mods) {
				auto now = Utils::Now();
				if (SI.modified == now)
					SI.modified++;
				else
					SI.modified = now;
				StorageService()->SubInfoDB().UpdateRecord("id", UserInfo_.userinfo.id, SI);
			}

			Poco::JSON::Object Answer;
			SI.to_json(Answer);
			return ReturnObject(Answer);
		}

		//  if the user does not have a device, we cannot continue.
		ProvObjects::SubscriberDeviceList Devices;

		if (!SDK::Prov::Subscriber::GetDevices(this, UserInfo_.userinfo.id,
											   UserInfo_.userinfo.owner, Devices)) {
			return BadRequest(RESTAPI::Errors::ProvServiceNotAvailable);
		}

		if (Devices.subscriberDevices.empty()) {
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		Logger().information(
			fmt::format("{}: Creating default user information.", UserInfo_.userinfo.email));
		StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI, Devices);
		Logger().information(
			fmt::format("{}: Fetching Current configuration from controller", UserInfo_.userinfo.email));
		ConfigMaker InitialConfig(Logger(), SI.id);
		
		ProvObjects::SubscriberDevice subDevice;
		const std::string targetMac = SI.accessPoints.list[0].macAddress;

		if (!SDK::Prov::Subscriber::GetDevice(this, targetMac, subDevice)) {
			Logger().error(fmt::format("Could not find provisioning subdevice for {}.", targetMac));
			return InternalError(RESTAPI::Errors::ConfigBlockInvalid);
		}
		if (!InitialConfig.PrepareDefaultConfig(SI.accessPoints.list[0], subDevice)) {
			Logger().error(fmt::format("Failed to create PrepareDefaultConfig: Fetched config is invalid for MAC {}.", SI.accessPoints.list[0].macAddress));
			return InternalError(RESTAPI::Errors::ConfigBlockInvalid);
		}
		if (!SDK::Prov::Subscriber::SetDevice(this, subDevice)) {
			Logger().error(fmt::format("Failed to persist provisioning config for {}.", subDevice.serialNumber));
			return InternalError(RESTAPI::Errors::ConfigBlockInvalid);
		}
		if (!SDK::GW::Device::SetSubscriber(this, subDevice.serialNumber, SI.id)) {
			Logger().error(fmt::format("Failed to link device {} to subscriber {} in gateway.", subDevice.serialNumber, SI.id));
		}
		if (!SDK::Prov::Subscriber::UpdateSubscriber(this, SI.id, subDevice.serialNumber, false)) {
			Logger().error(fmt::format("Couldn't link device {} to subscriber {} in inventory.", subDevice.serialNumber, SI.id));
		}
		Logger().information(fmt::format("{}: Creating default configuration.", UserInfo_.userinfo.email));
		StorageService()->SubInfoDB().CreateRecord(SI);
		StorageService()->SubInfoDB().GetRecord("id", SI.id, SI);

		Poco::JSON::Object Answer;
		SI.to_json(Answer);
		ReturnObject(Answer);
	}

	static bool ValidIPv4(const std::string &IP) {
		if (IP.empty())
			return false;
		Poco::Net::IPAddress A;

		if (!Poco::Net::IPAddress::tryParse(IP, A) || A.family() != Poco::Net::AddressFamily::IPv4)
			return false;

		return true;
	}

	[[maybe_unused]] static bool ValidIPv6(const std::string &IP) {
		if (IP.empty())
			return false;
		Poco::Net::IPAddress A;

		if (!Poco::Net::IPAddress::tryParse(IP, A) || A.family() != Poco::Net::AddressFamily::IPv6)
			return false;

		return true;
	}

	[[maybe_unused]] static bool ValidIPv4v6(const std::string &IP) {
		if (IP.empty())
			return false;
		Poco::Net::IPAddress A;

		if (!Poco::Net::IPAddress::tryParse(IP, A))
			return false;

		return true;
	}

	static bool ValidateIPv4Subnet(const std::string &IP) {
		auto IPParts = Poco::StringTokenizer(IP, "/");
		if (IPParts.count() != 2) {
			return false;
		}
		if (!ValidIPv4(IPParts[0])) {
			return false;
		}
		auto X = std::atoll(IPParts[1].c_str());
		if (X < 8 || X > 24) {
			return false;
		}
		return true;
	}

	void RESTAPI_subscriber_handler::DoPut() {

		auto ConfigChanged = GetParameter("configChanged", "true") == "true";
		auto ApplyConfigOnly = GetParameter("applyConfigOnly", "true") == "true";

		if (UserInfo_.userinfo.id.empty()) {
			return NotFound();
		}

		SubObjects::SubscriberInfo Existing;
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, Existing)) {
			return NotFound();
		}

		if (ApplyConfigOnly) {
			ConfigMaker InitialConfig(Logger(), UserInfo_.userinfo.id);
			if (InitialConfig.Prepare())
				return OK();
			else
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
		}

		const auto &Body = ParsedBody_;
		SubObjects::SubscriberInfo Changes;
		if (!Changes.from_json(Body)) {
			return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
		}

		auto Now = Utils::Now();
		if (Body->has("firstName"))
			Existing.firstName = Changes.firstName;
		if (Body->has("initials"))
			Existing.initials = Changes.initials;
		if (Body->has("lastName"))
			Existing.lastName = Changes.lastName;
		if (Body->has("secondaryEmail") && Utils::ValidEMailAddress(Changes.secondaryEmail))
			Existing.secondaryEmail = Changes.secondaryEmail;
		if (Body->has("serviceAddress"))
			Existing.serviceAddress = Changes.serviceAddress;
		if (Body->has("billingAddress"))
			Existing.billingAddress = Changes.billingAddress;
		if (Body->has("phoneNumber"))
			Existing.phoneNumber = Changes.phoneNumber;
		Existing.modified = Now;

		//  Look at the access points
		for (auto &NewAP : Changes.accessPoints.list) {
			for (auto &ExistingAP : Existing.accessPoints.list) {
				if (NewAP.macAddress == ExistingAP.macAddress) {
					for (const auto &ssid : NewAP.wifiNetworks.wifiNetworks) {
						if (ssid.password.length() < 8 || ssid.password.length() > 32) {
							return BadRequest(RESTAPI::Errors::SSIDInvalidPassword);
						}
					}
					if (NewAP.deviceMode.type == "nat") {
						if (!ValidIPv4(NewAP.deviceMode.startIP) ||
							!ValidIPv4(NewAP.deviceMode.endIP)) {
							return BadRequest(RESTAPI::Errors::InvalidStartingIPAddress);
						}
						if (!ValidateIPv4Subnet(NewAP.deviceMode.subnet)) {
							return BadRequest(RESTAPI::Errors::SubnetFormatError);
						}
					} else if (NewAP.deviceMode.type == "bridge") {

					} else if (NewAP.deviceMode.type == "manual") {
						if (!ValidateIPv4Subnet(NewAP.deviceMode.subnet)) {
							return BadRequest(RESTAPI::Errors::DeviceModeError);
						}
						if (!ValidIPv4(NewAP.deviceMode.startIP)) {
							return BadRequest(RESTAPI::Errors::DeviceModeError);
						}
						if (!ValidIPv4(NewAP.deviceMode.endIP)) {
							return BadRequest(RESTAPI::Errors::DeviceModeError);
						}
					} else {
						return BadRequest(RESTAPI::Errors::BadDeviceMode);
					}

					if (NewAP.internetConnection.type == "manual") {
						if (!ValidateIPv4Subnet(NewAP.internetConnection.subnetMask)) {
							return BadRequest(RESTAPI::Errors::SubnetFormatError);
						}
						if (!ValidIPv4(NewAP.internetConnection.defaultGateway)) {
							return BadRequest(RESTAPI::Errors::DefaultGatewayFormat);
						}
						if (!ValidIPv4(NewAP.internetConnection.primaryDns)) {
							return BadRequest(RESTAPI::Errors::PrimaryDNSFormat);
						}
						if (!NewAP.internetConnection.secondaryDns.empty() &&
							!ValidIPv4(NewAP.internetConnection.secondaryDns)) {
							return BadRequest(RESTAPI::Errors::SecondaryDNSFormat);
						}
					} else if (NewAP.internetConnection.type == "pppoe") {

					} else if (NewAP.internetConnection.type == "automatic") {

					} else {
						return BadRequest(RESTAPI::Errors::BadConnectionType);
					}

					ExistingAP = NewAP;
					ExistingAP.internetConnection.modified = Now;
					ExistingAP.deviceMode.modified = Now;
					ExistingAP.wifiNetworks.modified = Now;
					ExistingAP.subscriberDevices.modified = Now;
				}
			}
			Changes.modified = Utils::Now();
		}

		if (StorageService()->SubInfoDB().UpdateRecord("id", UserInfo_.userinfo.id, Existing)) {
			if (ConfigChanged) {
				ConfigMaker InitialConfig(Logger(), UserInfo_.userinfo.id);
				InitialConfig.Prepare();
			}
			SubObjects::SubscriberInfo Modified;
			StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, Modified);
			SubscriberCache()->UpdateSubInfo(UserInfo_.userinfo.id, Modified);
			Poco::JSON::Object Answer;
			Modified.to_json(Answer);
			return ReturnObject(Answer);
		}
		return InternalError(RESTAPI::Errors::RecordNotUpdated);
	}

	void RESTAPI_subscriber_handler::DoDelete() {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().warning("Received delete request without id.");
			return NotFound();
		}

		Logger().information(fmt::format("Deleting subscriber {}.", UserInfo_.userinfo.id));
		SubObjects::SubscriberInfo SI;
		if (StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI)) {
			for (const auto &i : SI.accessPoints.list) {
				if (!i.serialNumber.empty()) {
					SDK::Prov::Subscriber::UpdateSubscriber(nullptr, UserInfo_.userinfo.id,
																   i.serialNumber, true);
					SDK::GW::Device::SetSubscriber(nullptr, i.serialNumber, "");
				}
			}
			Logger().information(fmt::format("Deleting security info for {}.",
											 UserInfo_.userinfo.id));
			SDK::Sec::Subscriber::Delete(nullptr, UserInfo_.userinfo.id);
			Logger().information(fmt::format("Deleting subscriber info for {}.",
											 UserInfo_.userinfo.id));
			StorageService()->SubInfoDB().DeleteRecord("id", UserInfo_.userinfo.id);
			return OK();
		}
		return NotFound();
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
	ADD_DEVICE_VALIDATE_INPUTS:
	1. Ensure subscriber id is present.
	2. Ensure mac parameter is present and valid.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_VALIDATE_INPUTS(AddDeviceContext &ctx) {

		if (UserInfo_.userinfo.id.empty()) {
			Logger().error("Subscriber id is missing.");
			NotFound();
			return false;
		}

		ctx.Mac = GetParameter("mac", "");
		if (!Utils::NormalizeMac(ctx.Mac)) {
			Logger().error(fmt::format("Invalid MAC: [{}]", ctx.Mac));
			BadRequest(RESTAPI::Errors::InvalidSerialNumber);
			return false;
		}
		return true;
	}

/*
	ADD_DEVICE_VALIDATE_INVENTORY_OWNERSHIP:
	1. Ensure inventory has a record for the provided MAC.
	2. Ensure the device is not already provisioned to another subscriber.
	3. If already linked to this subscriber, check if device exists in SubInfoDB and return error if found.
	4. If linked to same subcriber but not in SubInfoDB, proceed with adding the device as Gateway or Mesh.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_VALIDATE_INVENTORY_OWNERSHIP(AddDeviceContext &ctx) {

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
	ADD_DEVICE_LOAD_SUBSCRIBER_INFO:
	1. Load the subscriber information from SubInfoDB to decide if new device should be added as a gateway or mesh.
	2. Return true if successful.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_LOAD_SUBSCRIBER_INFO(AddDeviceContext &ctx) {

		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, ctx.SubscriberInfo)) {
			Logger().error(fmt::format("Failed to fetch subscriber information for id: {}.", UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::RecordNotFound);
			return false;
		}
		return true;
	}

/*
	ADD_DEVICE_SETUP_GATEWAY:
	1. Create provisioning-subdevice table record for gateway device.
	2. Create gateway device record in SubInfoDB.
	2. Prepare default configuration for the gateway device.
	3. Persist the configuration in provisioning.
	4. Send the configuration to the device.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_SETUP_GATEWAY(AddDeviceContext &ctx) {

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
	ADD_DEVICE_SETUP_MESH:
	1. Ensure there is an existing gateway device for the subscriber.
	1. Fetch the gateway configuration and validate its mesh config block.
	3. Create provisioning subdevice record.
	4. Build the mesh configuration.
	5. Prepare/persist the configuration in provisioning-subdevice database and send config to device.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_SETUP_MESH(AddDeviceContext &ctx) {

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
	ADD_DEVICE_UPDATE_DB:
	1. Link the device to the subscriber in gateway.
	2. Link the device to the subscriber in provisioning.
	3. Update SubInfoDB with new device information.
*/
	bool RESTAPI_subscriber_handler::ADD_DEVICE_UPDATE_DB(AddDeviceContext &ctx) {

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
	void RESTAPI_subscriber_handler::DoPost() {
		Logger().information(fmt::format("Processing Add Device request for subscriber: {}.", UserInfo_.userinfo.id));

		AddDeviceContext ctx;

		if (!ADD_DEVICE_VALIDATE_INPUTS(ctx)) return;
		if (!ADD_DEVICE_VALIDATE_INVENTORY_OWNERSHIP(ctx)) return;
		if (!ADD_DEVICE_LOAD_SUBSCRIBER_INFO(ctx)) return;

		if (ctx.SubscriberInfo.accessPoints.list.empty()) {
			if (!ADD_DEVICE_SETUP_GATEWAY(ctx)) return;
		} else {
			if (!ADD_DEVICE_SETUP_MESH(ctx)) return;
		}

		if (!ADD_DEVICE_UPDATE_DB(ctx)) return;

		return OK();
	}
} // namespace OpenWifi
