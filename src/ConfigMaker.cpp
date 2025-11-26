/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-12-13.
//

#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "framework/utils.h"
#include "framework/ow_constants.h"
#include "nlohmann/json.hpp"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {

	static std::string ConvertBand(const std::string &B) {
		if (B == "2G")
			return "2G";
		if (B == "6G")
			return "6G";
		if (B == "5GU")
			return "5G-upper";
		if (B == "5GL")
			return "5G-lower";
		return B;
	}

	static std::vector<std::string> ConvertBands(const std::vector<std::string> &Bs) {
		std::vector<std::string> R;
		for (const auto &i : Bs)
			R.emplace_back(ConvertBand(i));
		return R;
	}

	void CreateDHCPInfo(std::string &Subnet, const std::string &First, const std::string &Last,
						uint64_t &DHCPFirst, uint64_t &HowMany) {
		Poco::Net::IPAddress SubnetAddr, FirstAddress, LastAddress;
		auto Tokens = Poco::StringTokenizer(Subnet, "/");
		if (!Poco::Net::IPAddress::tryParse(Tokens[0], SubnetAddr) ||
			!Poco::Net::IPAddress::tryParse(First, FirstAddress) ||
			!Poco::Net::IPAddress::tryParse(Last, LastAddress)) {
			Subnet = "192.168.1.1/24";
			DHCPFirst = 10;
			HowMany = 100;
			return;
		}

		if (LastAddress < FirstAddress)
			std::swap(LastAddress, FirstAddress);

		struct in_addr FA {
			*static_cast<const in_addr *>(FirstAddress.addr())
		}, LA{*static_cast<const in_addr *>(LastAddress.addr())};

		HowMany = htonl(LA.s_addr) - htonl(FA.s_addr);
		auto SubNetBits = std::stoull(Tokens[1], nullptr, 10);
		uint64_t SubNetBitMask;
		if (SubNetBits == 8)
			SubNetBitMask = 0x000000ff;
		else if (SubNetBits == 16)
			SubNetBitMask = 0x0000ffff;
		else
			SubNetBitMask = 0x000000ff;
		DHCPFirst = htonl(FA.s_addr) & SubNetBitMask;
	}

	/*
		This function prepares and updates the provisioning configuration for an Access Point (AP)
		using the configuration fetched from the controller (owgw).

		Example of configuration received from controller:
		{
			"interfaces": [ ... ],
			"metrics": { ... },
			"radios": [ ... ],
			"services": { ... },
			"uuid": "some-uuid"
		}

		Goal:
		We need to convert this controller configuration into a provisioning format that can be stored
		in the provisioning database.

		Conversion Rules:
		- Keep only sections that are JSON objects or arrays (ignore simple key-value pairs).
		- For each section (like "interfaces", "metrics", "radios", "services"):
			- Add a "name" (section name)
			- Add a "weight" (default: 1)
			- Store its full JSON data as a configuration element

		Example of resulting provisioning configuration format:
		{
			"interfaces": {
				"name": "interfaces",
				"weight": 1,
				...
			},
			"metrics": {
				"name": "metrics",
				"weight": 1,
				...
			},
			"radios": {
				"name": "radios",
				"weight": 1,
				...
			},
			"services": {
				"name": "services",
				"weight": 1,
				...
			}
		}

		In summary:
		The function extracts each relevant section (object or array) from the controller config,
		adds name and weight to it, and stores it in the provisioning database for that AP.
	*/
	bool UpdateSubDevices(const Poco::JSON::Object::Ptr &Config, const std::string &serial, Poco::Logger &logger) {
		ProvObjects::SubscriberDevice subDevice;
		if (!SDK::Prov::Subscriber::GetDevice(nullptr, serial, subDevice)) {
			logger.error(fmt::format("Could not find provisioning subdevice for {}", serial));
			return false;
		}
		std::vector<std::string> sectionNames;
		Config->getNames(sectionNames);
		for (size_t i = 0; i < sectionNames.size(); ++i) {
			const std::string &name = sectionNames[i];
			if (!(Config->isObject(name) || Config->isArray(name))) {
				logger.information(fmt::format("Skipping extra key value '{}' for device {}", name, serial));
				continue;
			}
			Poco::JSON::Object::Ptr sectionObj = new Poco::JSON::Object();
			if (Config->isArray(name))
				sectionObj->set(name, Config->getArray(name));
			else
				sectionObj->set(name, Config->getObject(name));
			std::ostringstream jsonOutput;
			Poco::JSON::Stringifier::stringify(sectionObj, jsonOutput);
			ProvObjects::DeviceConfigurationElement elem;
			elem.name = name;
			elem.weight = DEFAULT_DEVICE_CONFIG_WEIGHT;
			elem.configuration = jsonOutput.str();
			subDevice.configuration.push_back(elem);
			logger.information(fmt::format("Stored section {} for device {}", name, serial));
		}
		if (!SDK::Prov::Subscriber::SetDevice(nullptr, subDevice)) {
			logger.error(fmt::format("Cannot update provisioning subdevice for {}", serial));
			return false;
		}
		return true;
	}

	/*
		DefaultConfig() will:
		1. For each AP of the subscriber, fetch current config from controller (owgw) (/api/v1/device/MAC)
		2. Validate the fetched config: reject upstream interfaces carrying SSIDs and configs missing a mesh SSID.
		3. If valid, set SSID/password based on the device MAC (AP + mesh).
		4. Call UpdateSubDevices() to update provisioning, then push the updated config to the device
		   (/api/v1/inventory/MAC?applyConfiguration=true).
	*/
	bool OpenWifi::ConfigMaker::DefaultConfig(const SubObjects::SubscriberInfo &SI) {
		for (size_t i = 0; i < SI.accessPoints.list.size(); ++i) {
			const auto &ap = SI.accessPoints.list[i];
			Poco::JSON::Object::Ptr ConfigObj;
			auto ResponseStatus = SDK::GW::Device::GetConfig(nullptr, ap.serialNumber, ConfigObj);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Logger_.warning(fmt::format("Failed to fetch current configuration for device {}.",
					ap.serialNumber));
				return false;
			}
			auto NewConfig = ConfigObj->getObject("configuration");
			auto interfaces = NewConfig->getArray("interfaces");
			bool meshSsidFound = false;
			for (std::size_t i = 0; i < interfaces->size(); ++i) {
				auto iface = interfaces->getObject(i);
				auto ssids = iface->getArray("ssids");
				std::string role = iface->getValue<std::string>("role");
				if (role == "upstream" && ssids && ssids->size() > 0) {
					Logger_.error(fmt::format("Invalid configuration for device {}: upstream interface contains SSIDs.", ap.serialNumber));
					return false;
				}
				if (!ssids)
					continue;
				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (ssid->getValue<std::string>("bss-mode") == "mesh")
						meshSsidFound = true;
				}
			}
			if (!meshSsidFound) {
				Logger_.error(fmt::format("Invalid configuration for device {}: missing mesh SSID.", ap.serialNumber));
				return false;
			}
			std::string NewSsid = DEFAULT_SSID_PREFIX + ap.macAddress.substr(MAC_SUFFIX_START_INDEX);
			std::string NewPassword = Poco::toUpper(ap.macAddress);
			for (std::size_t i = 0; i < interfaces->size(); ++i) {
				auto iface = interfaces->getObject(i);
				auto ssids = iface->getArray("ssids");
				if (!ssids)
					continue; // if no ssids to update, skip this interface
				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					auto encryption = ssid->getObject("encryption");
					auto bssMode = ssid->getValue<std::string>("bss-mode");
					if (bssMode == "ap") {
						ssid->set("name", NewSsid);
						encryption->set("key", NewPassword);
					} else if (bssMode == "mesh") {
						encryption->set("key", NewPassword);
					}
				}
			}
			if (UpdateSubDevices(NewConfig, ap.serialNumber, Logger_)) {
				Logger_.information(fmt::format("Updated configuration for device {} in provisioning", ap.serialNumber));
				ProvObjects::InventoryConfigApplyResult applyResult;
				if (SDK::Prov::Configuration::Push(nullptr, ap.serialNumber, applyResult)) {
					Logger_.information(fmt::format("Provisioning applied updated configuration to device {}.", ap.serialNumber));
				} else {
					Logger_.error(fmt::format("Provisioning failed to apply updated configuration to device {}.", ap.serialNumber));
				}
				SDK::GW::Device::SetSubscriber(nullptr, ap.serialNumber, SI.id);
				SDK::Prov::Subscriber::UpdateSubscriber(nullptr, SI.id, ap.serialNumber, false);
			} else {
				Logger_.error(fmt::format("Failed to store updated configuration for device {} in provisioning.", ap.serialNumber));
			}
		}
		return true;
	}
	bool ConfigMaker::Prepare() {

		SubObjects::SubscriberInfo SI;
		if (!StorageService()->SubInfoDB().GetRecord("id", id_, SI)) {
			bad_ = true;
			return false;
		}

		//  We need to create the basic sections
		auto metrics = R"(
            {
              "metrics": {
                "dhcp-snooping": {
                  "filters": [
                    "ack",
                    "discover",
                    "offer",
                    "request",
                    "solicit",
                    "reply",
                    "renew"
                  ]
                },
                "health": {
                  "interval": 60
                },
                "statistics": {
                  "interval": 60,
                  "types": [
                    "ssids",
                    "lldp",
                    "clients"
                  ]
                },
                "wifi-frames": {
                  "filters": [
                    "probe",
                    "auth",
                    "assoc",
                    "disassoc",
                    "deauth",
                    "local-deauth",
                    "inactive-deauth",
                    "key-mismatch",
                    "beacon-report",
                    "radar-detected"
                  ]
                }
              }
            }
         )"_json;

		auto services = R"(
        {
            "services": {
                "lldp": {
                    "describe": "uCentral",
                    "location": "universe"
                },
                "ssh": {
                    "authorized-keys": [],
                    "password-authentication": false,
                    "port": 22
                }
            }
        } )"_json;

		for (auto &i : SI.accessPoints.list) {

			nlohmann::json Interfaces;
			nlohmann::json UpstreamInterface;
			nlohmann::json DownstreamInterface;
			nlohmann::json radios;

			if (i.macAddress.empty())
				continue;

			Logger_.information(fmt::format("{}: Generating configuration.", i.macAddress));

			UpstreamInterface["name"] = "WAN";
			UpstreamInterface["role"] = "upstream";
			UpstreamInterface["services"].push_back("lldp");

			std::vector<std::string> AllBands;
			for (const auto &rr : i.radios)
				AllBands.emplace_back(ConvertBand(rr.band));

			nlohmann::json UpstreamPort, DownstreamPort;
			if (i.internetConnection.type == "manual") {
				UpstreamInterface["addressing"] = "static";
				UpstreamInterface["subnet"] = i.internetConnection.subnetMask;
				UpstreamInterface["gateway"] = i.internetConnection.defaultGateway;
				UpstreamInterface["send-hostname"] = i.internetConnection.sendHostname;
				UpstreamInterface["use-dns"].push_back(i.internetConnection.primaryDns);
				if (!i.internetConnection.secondaryDns.empty())
					UpstreamInterface["use-dns"].push_back(i.internetConnection.secondaryDns);
			} else if (i.internetConnection.type == "pppoe") {
				nlohmann::json Port;
				Port["select-ports"].push_back("WAN*");
				UpstreamInterface["ethernet"].push_back(Port);
				UpstreamInterface["broad-band"]["protocol"] = "pppoe";
				UpstreamInterface["broad-band"]["user-name"] = i.internetConnection.username;
				UpstreamInterface["broad-band"]["password"] = i.internetConnection.password;
				UpstreamInterface["ipv4"]["addressing"] = "dynamic";
				if (i.internetConnection.ipV6Support)
					UpstreamInterface["ipv6"]["addressing"] = "dynamic";
			} else if (i.internetConnection.type == "automatic") {
				nlohmann::json Port;
				Port["select-ports"].push_back("WAN*");
				if (i.deviceMode.type == "bridge")
					Port["select-ports"].push_back("LAN*");
				UpstreamInterface["ethernet"].push_back(Port);
				UpstreamInterface["ipv4"]["addressing"] = "dynamic";
				if (i.internetConnection.ipV6Support)
					UpstreamInterface["ipv6"]["addressing"] = "dynamic";
			}

			if (i.deviceMode.type == "bridge") {
				UpstreamPort["select-ports"].push_back("LAN*");
				UpstreamPort["select-ports"].push_back("WAN*");
			} else if (i.deviceMode.type == "manual") {
				UpstreamPort.push_back("WAN*");
				DownstreamPort.push_back("LAN*");
				DownstreamInterface["name"] = "LAN";
				DownstreamInterface["role"] = "downstream";
				DownstreamInterface["services"].push_back("lldp");
				DownstreamInterface["services"].push_back("ssh");
				DownstreamInterface["ipv4"]["addressing"] = "static";
				uint64_t HowMany = 0;
				uint64_t FirstIPInRange;
				CreateDHCPInfo(i.deviceMode.subnet, i.deviceMode.startIP, i.deviceMode.endIP,
							   FirstIPInRange, HowMany);
				DownstreamInterface["ipv4"]["subnet"] = i.deviceMode.subnet;
				DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = FirstIPInRange;
				DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = HowMany;
				DownstreamInterface["ipv4"]["dhcp"]["lease-time"] =
					i.deviceMode.leaseTime.empty() ? "24h" : i.deviceMode.leaseTime;
			} else if (i.deviceMode.type == "nat") {
				UpstreamPort["select-ports"].push_back("WAN*");
				DownstreamPort["select-ports"].push_back("LAN*");
				DownstreamInterface["name"] = "LAN";
				DownstreamInterface["role"] = "downstream";
				DownstreamInterface["services"].push_back("lldp");
				DownstreamInterface["services"].push_back("ssh");
				DownstreamInterface["ipv4"]["addressing"] = "static";
				uint64_t HowMany = 0;
				uint64_t FirstIPInRange;
				CreateDHCPInfo(i.deviceMode.subnet, i.deviceMode.startIP, i.deviceMode.endIP,
							   FirstIPInRange, HowMany);
				DownstreamInterface["ipv4"]["subnet"] = i.deviceMode.subnet;
				DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = FirstIPInRange;
				DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = HowMany;
				DownstreamInterface["ipv4"]["dhcp"]["lease-time"] =
					i.deviceMode.leaseTime.empty() ? "24h" : i.deviceMode.leaseTime;
			}
			bool hasGuest = false;
			nlohmann::json main_ssids, guest_ssids;
			for (const auto &j : i.wifiNetworks.wifiNetworks) {
				nlohmann::json ssid;
				ssid["name"] = j.name;
				if (j.bands[0] == "all") {
					ssid["wifi-bands"] = AllBands;
				} else {
					ssid["wifi-bands"] = ConvertBands(j.bands);
				}
				ssid["bss-mode"] = "ap";
				if (j.encryption == "wpa1-personal") {
					ssid["encryption"]["proto"] = "psk";
					ssid["encryption"]["ieee80211w"] = "disabled";
				} else if (j.encryption == "wpa2-personal") {
					ssid["encryption"]["proto"] = "psk2";
					ssid["encryption"]["ieee80211w"] = "disabled";
				} else if (j.encryption == "wpa3-personal") {
					ssid["encryption"]["proto"] = "sae";
					ssid["encryption"]["ieee80211w"] = "required";
				} else if (j.encryption == "wpa1/2-personal") {
					ssid["encryption"]["proto"] = "psk-mixed";
					ssid["encryption"]["ieee80211w"] = "disabled";
				} else if (j.encryption == "wpa2/3-personal") {
					ssid["encryption"]["proto"] = "sae-mixed";
					ssid["encryption"]["ieee80211w"] = "disabled";
				}
				ssid["encryption"]["key"] = j.password;
				if (j.type == "main") {
					main_ssids.push_back(ssid);
				} else {
					hasGuest = true;
					ssid["isolate-clients"] = true;
					guest_ssids.push_back(ssid);
				}
			}

			if (i.deviceMode.type == "bridge")
				UpstreamInterface["ssids"] = main_ssids;
			else
				DownstreamInterface["ssids"] = main_ssids;

			nlohmann::json UpStreamEthernet, DownStreamEthernet;
			if (!UpstreamPort.empty()) {
				UpStreamEthernet.push_back(UpstreamPort);
			}
			if (!DownstreamPort.empty()) {
				DownStreamEthernet.push_back(DownstreamPort);
			}

			if (i.deviceMode.type == "bridge") {
				UpstreamInterface["ethernet"] = UpStreamEthernet;
				Interfaces.push_back(UpstreamInterface);
			} else {
				UpstreamInterface["ethernet"] = UpStreamEthernet;
				DownstreamInterface["ethernet"] = DownStreamEthernet;
				Interfaces.push_back(UpstreamInterface);
				Interfaces.push_back(DownstreamInterface);
			}

			if (hasGuest) {
				nlohmann::json GuestInterface;
				GuestInterface["name"] = "Guest";
				GuestInterface["role"] = "downstream";
				GuestInterface["isolate-hosts"] = true;
				GuestInterface["ipv4"]["addressing"] = "static";
				GuestInterface["ipv4"]["subnet"] = "192.168.10.1/24";
				GuestInterface["ipv4"]["dhcp"]["lease-first"] = (uint64_t)10;
				GuestInterface["ipv4"]["dhcp"]["lease-count"] = (uint64_t)100;
				GuestInterface["ipv4"]["dhcp"]["lease-time"] = "6h";
				GuestInterface["ssids"] = guest_ssids;
				Interfaces.push_back(GuestInterface);
			}

			for (const auto &k : i.radios) {
				nlohmann::json radio;

				radio["band"] = ConvertBand(k.band);
				radio["bandwidth"] = k.bandwidth;

				if (k.channel == 0)
					radio["channel"] = "auto";
				else
					radio["channel"] = k.channel;
				if (k.country.size() == 2)
					radio["country"] = k.country;

				radio["channel-mode"] = k.channelMode;
				radio["channel-width"] = k.channelWidth;
				if (!k.requireMode.empty())
					radio["require-mode"] = k.requireMode;
				if (k.txpower > 0)
					radio["tx-power"] = k.txpower;
				if (k.allowDFS)
					radio["allow-dfs"] = true;
				if (!k.mimo.empty())
					radio["mimo"] = k.mimo;
				radio["legacy-rates"] = k.legacyRates;
				radio["beacon-interval"] = k.beaconInterval;
				radio["dtim-period"] = k.dtimPeriod;
				radio["maximum-clients"] = k.maximumClients;
				radio["rates"]["beacon"] = k.rates.beacon;
				radio["rates"]["multicast"] = k.rates.multicast;
				radio["he-settings"]["multiple-bssid"] = k.he.multipleBSSID;
				radio["he-settings"]["ema"] = k.he.ema;
				radio["he-settings"]["bss-color"] = k.he.bssColor;
				radios.push_back(radio);
			}

			ProvObjects::DeviceConfigurationElement Metrics{.name = "metrics",
															.description = "default metrics",
															.weight = 0,
															.configuration = to_string(metrics)};

			ProvObjects::DeviceConfigurationElement Services{.name = "services",
															 .description = "default services",
															 .weight = 0,
															 .configuration = to_string(services)};

			nlohmann::json InterfaceSection;
			InterfaceSection["interfaces"] = Interfaces;
			ProvObjects::DeviceConfigurationElement InterfacesList{
				.name = "interfaces",
				.description = "default interfaces",
				.weight = 0,
				.configuration = to_string(InterfaceSection)};

			nlohmann::json RadiosSection;
			RadiosSection["radios"] = radios;
			ProvObjects::DeviceConfigurationElement RadiosList{.name = "radios",
															   .description = "default radios",
															   .weight = 0,
															   .configuration =
																   to_string(RadiosSection)};

			ProvObjects::SubscriberDevice SubDevice;

			if (SDK::Prov::Subscriber::GetDevice(nullptr, i.serialNumber, SubDevice)) {
				SubDevice.configuration.clear();
				SubDevice.configuration.push_back(Metrics);
				SubDevice.configuration.push_back(Services);
				SubDevice.configuration.push_back(InterfacesList);
				SubDevice.configuration.push_back(RadiosList);
				SubDevice.deviceRules.firmwareUpgrade = i.automaticUpgrade ? "yes" : "no";
				if (SDK::Prov::Subscriber::SetDevice(nullptr, SubDevice)) {
					Logger_.information(
						fmt::format("Updating configuration for {}", i.serialNumber));
				} else {
					Logger_.information(
						fmt::format("Cannot update configuration for {}", i.serialNumber));
				}
			} else {
				Logger_.information(fmt::format(
					"Could not find Subscriber device in provisioning for {}", i.serialNumber));
			}
			SDK::GW::Device::SetSubscriber(nullptr, i.serialNumber, SI.id);
			SDK::Prov::Subscriber::UpdateSubscriber(nullptr, SI.id, i.serialNumber, false);
		}
		SI.modified = Utils::Now();
		return StorageService()->SubInfoDB().UpdateRecord("id", id_, SI);
	}

} // namespace OpenWifi
