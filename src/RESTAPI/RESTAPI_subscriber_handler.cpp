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
	Flow of GET /api/v1/subscriber when the subscriber calls this api for the first time:
	1. RESTAPI_subscriber_handler::DoGet() calls ConfigMaker::DefaultConfig()
	2. DefaultConfig() will fetch the current config from controller(owgw) (/api/v1/device/MAC)
	3. It will validate the config (no SSIDs on upstream, mesh SSID present), then update ssid/password based on MAC.
	4. Then it will call UpdateSubDevices() to update the provisioning database with the new config and push it.
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
		if (!InitialConfig.DefaultConfig(SI)) {
			Logger().error(fmt::format("Failed to create DefaultConfig: Fetched config is invalid for MAC {}.", SI.accessPoints.list[0].macAddress));
			return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
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

	/*
		Flow of POST /api/v1/subscriber (add device):
		1. Validate subscriber id and mac parameter; ensure inventory has a matching record.
		2. Reject if the device is already assigned (other subscriber or same subscriber).
		3. Fetch config from the primary device to use as a template.
		4. Create a subdevice data in provisioning for this new device.
		5. Build mesh config, store it in provisioning, push it, and link the device in provisioning/gateway.
	*/
	void RESTAPI_subscriber_handler::DoPost() {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().error(fmt::format("Subscriber id is missing."));
			return NotFound();
		}
		const auto mac = GetParameter("mac", "");
		std::string newDeviceMac = mac;
		if (mac.empty() || !Utils::NormalizeMac(newDeviceMac)) {
			Logger().error(fmt::format("Missing or invalid 'mac' query parameter '{}'", mac));
			return BadRequest(RESTAPI::Errors::InvalidSerialNumber);
		}
		ProvObjects::InventoryTag inventoryTag;
		if (!SDK::Prov::Device::Get(this, newDeviceMac, inventoryTag)) {
			Logger().error(fmt::format("Provisioning has no record for device {}.", newDeviceMac));
			return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}
		if (!inventoryTag.subscriber.empty()) {
			if (inventoryTag.subscriber != UserInfo_.userinfo.id) {
				Logger().error(fmt::format("Device {} is already assigned to another subscriber {}.", newDeviceMac, inventoryTag.subscriber));
			} else {
				Logger().error(fmt::format("Device {} is already registered for this subscriber.", newDeviceMac));
			}
			return BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
		}
		SubObjects::SubscriberInfo SI;
		const bool hasSubInfo = StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI);
		if (!hasSubInfo || SI.accessPoints.list.empty()) { // If no existing APs -> treate new device as gatewayAP
			ProvObjects::SubscriberDevice provisionedDevice;
			if (!SDK::Prov::Subscriber::CreateSubDeviceData(this, inventoryTag, UserInfo_.userinfo, provisionedDevice)) {
				Logger().error(fmt::format("Failed to create subdevice record for device {} in owprov.", newDeviceMac));
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
			}
			ProvObjects::SubscriberDeviceList devices;
			devices.subscriberDevices.push_back(provisionedDevice); // Ensure the new device is in subDevices list for SubInfoDB creation
				StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI, devices);
				ConfigMaker InitialConfig(Logger(), SI.id);
				if (!InitialConfig.DefaultConfig(SI)) {
					Logger().error(fmt::format("Failed to create default config for device {}.", newDeviceMac));
					return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
				}
				if (hasSubInfo) {
					Logger().information(fmt::format("Updating subscriber info for {}.", UserInfo_.userinfo.id));
					StorageService()->SubInfoDB().UpdateRecord("id", SI.id, SI);
				} else {
					Logger().information(fmt::format("Creating subscriber info for {}.", UserInfo_.userinfo.id));
					StorageService()->SubInfoDB().CreateRecord(SI);
				}
				return OK();
		}
		const auto gatewayMac = SI.accessPoints.list.front().serialNumber;
		Poco::JSON::Object::Ptr gatewayConfig;
		auto status = SDK::GW::Device::GetConfig(this, gatewayMac, gatewayConfig);
		if (status != Poco::Net::HTTPResponse::HTTP_OK) {
			Logger().error(fmt::format("Failed to fetch current configuration from gateway {} (status {}).", gatewayMac, status));
			return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
		}
		auto configuration = gatewayConfig->getObject("configuration");
		if (!SDK::GW::Device::ValidateMeshSSID(configuration, gatewayMac, Logger())) {
			Logger().error(fmt::format("Wrong mesh configuration found on fetched gateway device {}", gatewayMac));
			return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
		}
		Logger().information(fmt::format("Fetched gateway configuration for {}.", gatewayMac));
		// Ensure provisioning has a subscriberDevice entry for new device before any inventory updates
		ProvObjects::SubscriberDevice provisionedDevice;
		if (!SDK::Prov::Subscriber::CreateSubDeviceData(this, inventoryTag, UserInfo_.userinfo, provisionedDevice)) {
			Logger().error(fmt::format("Failed to create subdevice record for device {} in owprov.", newDeviceMac));
			return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
		}
		try {
			auto meshConfig = SDK::Prov::Subscriber::BuildMeshConfig(configuration);
			if (!meshConfig) {
				Logger().error("Failed to convert mesh configuration for provisioning.");
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
			}
			if (!UpdateSubDevices(meshConfig, newDeviceMac, Logger())) {
				Logger().error(fmt::format("Failed to store configuration for device {} in provisioning.", newDeviceMac));
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
			}
			ProvObjects::InventoryConfigApplyResult applyResult;
			if (!SDK::Prov::Configuration::Push(this, newDeviceMac, applyResult)) {
				Logger().error(fmt::format("Provisioning failed to apply mesh configuration to device {}.", newDeviceMac));
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
			}
			if (!SDK::Prov::Subscriber::UpdateSubscriber(this, UserInfo_.userinfo.id, newDeviceMac, false)) {
				Logger().error(fmt::format("Couldn't link device {} to subscriber {} in inventory.", newDeviceMac, UserInfo_.userinfo.id));
				return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
			}
			SDK::GW::Device::SetSubscriber(this, newDeviceMac, UserInfo_.userinfo.id);
			// Update SubInfoDB accessPoints list with new device data
			SubObjects::AccessPoint newAp;
			newAp.macAddress = newDeviceMac;
			newAp.serialNumber = newDeviceMac;
			newAp.deviceType = inventoryTag.deviceType;
			newAp.id = provisionedDevice.info.id;
			newAp.name = "Access Point #" + std::to_string(SI.accessPoints.list.size() + 1);
			SI.accessPoints.list.push_back(newAp);
			StorageService()->SubInfoDB().UpdateRecord("id", SI.id, SI);
			return OK();
		} catch (const std::exception &ex) {
			Logger().error(fmt::format("Exception while building mesh config: {}", ex.what()));
			return InternalError(RESTAPI::Errors::InternalError);
		}
	}
} // namespace OpenWifi
