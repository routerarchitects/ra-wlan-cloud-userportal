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

namespace OpenWifi {
	bool ConfigMaker::ValidateConfig(const Poco::JSON::Object::Ptr &deviceConfig, const std::string &serialNumber, Poco::Logger &logger) {
		if (!deviceConfig || !deviceConfig->has("configuration") || !deviceConfig->isObject("configuration")) {
			logger.error(fmt::format("Invalid configuration for device {}: missing configuration block.", serialNumber));
			return false;
		}
		auto configuration = deviceConfig->getObject("configuration");
		if (!configuration) {
			logger.error(fmt::format("Invalid configuration for device {}: Empty configuration.", serialNumber));
			return false;
		}
		if (!configuration->has("interfaces") || !configuration->isArray("interfaces")) {
			logger.error(fmt::format("Invalid configuration for device {}: missing/invalid interfaces.", serialNumber));
			return false;
		}
		auto interfaces = configuration->getArray("interfaces");
		if (!interfaces || interfaces->size() == 0) {
			logger.error(fmt::format("Invalid configuration for device {}: missing interfaces.", serialNumber));
			return false;
		}
		bool downstreamFound = false;
		for (std::size_t i = 0; i < interfaces->size(); ++i) {
			auto iface = interfaces->getObject(i);
			if (!iface || !iface->has("role") || !iface->get("role").isString()) {
				logger.error(fmt::format("Invalid configuration for device {}: interface is not an object or missing/invalid role.", serialNumber));
				return false;
			}
			std::string role = iface->getValue<std::string>("role");
			Poco::JSON::Array::Ptr ssids;
			if (iface->has("ssids") && iface->isArray("ssids")) {
				ssids = iface->getArray("ssids");
			}
			if (role == "upstream") {
				if (ssids && !ssids->empty()) {
					logger.error(fmt::format("Invalid configuration for device {}: upstream interface contains SSIDs.",serialNumber));
					return false;
				}
			} else if (role == "downstream") {
				bool meshSsidFound = false;
				bool apSsidFound = false;
				downstreamFound = true;
				if (!iface->has("ipv4") || !iface->isObject("ipv4")) {
					logger.error(fmt::format("Invalid configuration for device {}: downstream interface missing or invalid IPv4 value.", serialNumber));
					return false;
				}
				auto ipv4 = iface->getObject("ipv4");
				if (!ipv4->has("addressing") || !ipv4->get("addressing").isString() || ipv4->getValue<std::string>("addressing") != "static") {
					logger.error(fmt::format("Invalid configuration for device {}: downstream interface should have static IPv4 addressing.", serialNumber));
					return false;
				}
				if (!iface->has("tunnel") || !iface->isObject("tunnel")) {
					logger.error(fmt::format("Invalid configuration for device {}: downstream interface missing or invalid tunnel object.", serialNumber));
					return false;
				}
				auto tunnel = iface->getObject("tunnel");
				if (!tunnel->has("proto") || !tunnel->get("proto").isString() || tunnel->getValue<std::string>("proto") != "mesh") {
					logger.error(fmt::format("Invalid configuration for device {}: downstream interface must have tunnel-proto='mesh'.", serialNumber));
					return false;
				}
				if (!ssids || ssids->empty()) {
					logger.error(fmt::format("Invalid configuration for device {}: downstream interface missing or invalid SSIDs.", serialNumber));
					return false;
				}
				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (!ssid) {
						logger.error(fmt::format("Invalid configuration for device {}: downstream interface contains a non-object SSID entry.", serialNumber));
						return false;
					}
					if (!ssid->has("bss-mode") || !ssid->get("bss-mode").isString()) {
						logger.error(fmt::format("Invalid configuration for device {}: SSID entry missing or invalid 'bss-mode'.", serialNumber));
						return false;
					}
					std::string mode = ssid->getValue<std::string>("bss-mode");
					if (mode == "ap") {
						apSsidFound = true;
					} else if (mode == "mesh") {
						meshSsidFound = true;
					}
				}
				if (!apSsidFound) {
					logger.error(fmt::format("Invalid configuration for device {}: missing ap-SSID on downstream interface.", serialNumber));
					return false;
				}
				if (!meshSsidFound) {
					logger.error(fmt::format("Invalid configuration for device {}: missing mesh-SSID on downstream interface.", serialNumber));
					return false;
				}
			} else {
				logger.error(fmt::format("Invalid configuration for device {}: invalid interface role (expected 'upstream' or 'downstream').", serialNumber));
				return false;
			}
		}
		if (!downstreamFound) {
			logger.error(fmt::format("Invalid configuration for device {}: missing downstream interface.", serialNumber));
			return false;
		}
		return true;
	}

	bool ConfigMaker::BuildMeshConfig(const Poco::JSON::Object::Ptr &InputConfig, Poco::JSON::Object::Ptr &OutputConfig) {
	if (!InputConfig || !InputConfig->has("configuration") || !InputConfig->isObject("configuration")) {
		Logger_.error("BuildMeshConfig: missing configuration block.");
		return false;
	}
	auto gatewayConfig = InputConfig->getObject("configuration");
	if (!gatewayConfig) {
		Logger_.error("BuildMeshConfig: empty configuration block.");
		return false;
	}
	try {
		std::ostringstream OS;
		Poco::JSON::Stringifier::stringify(gatewayConfig, OS);
		auto cfg = nlohmann::json::parse(OS.str());
		if (cfg.contains("interfaces") && cfg["interfaces"].is_array() && !cfg["interfaces"].empty()) {
			nlohmann::json meshInterfaces = nlohmann::json::array();
			for (const auto &iface : cfg["interfaces"]) {
				auto meshInterface = iface;
				meshInterface["ipv4"] = {{"addressing", "dynamic"}};
				meshInterfaces.push_back(meshInterface);
			}
			if (!meshInterfaces.empty()) {
				cfg["interfaces"] = meshInterfaces;
			}
		}

		// If config-raw contains our block-clients rule, don't send config-raw to mesh devices.
		if (cfg.contains("config-raw")) {
			for (const auto &cmd : cfg["config-raw"]) {
				if (!cmd.is_array() || cmd.size() < 3)
					continue;
				if (cmd[0] != "set" || cmd[1] != "firewall.@rule[-1].name" || !cmd[2].is_string())
					continue;
				const auto ruleName = cmd[2].get<std::string>();
				if (ruleName == "Block_Clients") {
					Logger_.information("Removing firewall-rule [Block_Clients] from config-raw for mesh device.");
					cfg.erase("config-raw");
					break;
				}
			}
		}
		Poco::JSON::Parser parser;
		auto parsed = parser.parse(cfg.dump());
		OutputConfig = parsed.extract<Poco::JSON::Object::Ptr>();
		return OutputConfig != nullptr;
	} catch (...) {
		Logger_.error("BuildMeshConfig: failed to parse/convert configuration.");
		return false;
	}
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
	bool ConfigMaker::PrepareProvSubDeviceConfig(const Poco::JSON::Object::Ptr &Config, ProvObjects::DeviceConfigurationElementVec &ProvConfig){
		// Clear existing configuration to prevent duplicate sections in case of retries
		ProvConfig.clear();
		std::vector<std::string> sectionNames;
		Config->getNames(sectionNames);
		for (size_t i = 0; i < sectionNames.size(); ++i) {
			const std::string &name = sectionNames[i];
			if (!(Config->isObject(name) || Config->isArray(name))) {
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
			ProvConfig.push_back(elem);
		}
		return true;
	}

	/*
		PrepareDefaultConfig():
		1. Fetch current config for the provided AccessPoint from the controller (/api/v1/device/MAC).
		2. Validate mesh settings (no upstream SSIDs, mesh present).
		3. Update SSID/password based on the device MAC (AP + mesh).
		4. Return the updated configuration object so the caller can decide how to persist/apply it.
	*/
	bool ConfigMaker::PrepareDefaultConfig(const SubObjects::AccessPoint &ap, ProvObjects::SubscriberDevice &subDevice) {
			Poco::JSON::Object::Ptr ConfigObj;
			auto ResponseStatus = SDK::GW::Device::GetConfig(nullptr, ap.serialNumber, ConfigObj);
			if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Logger_.error(fmt::format("Failed to fetch current configuration for device {}.", ap.serialNumber));
				return false;
			}
			if (!ValidateConfig(ConfigObj, ap.serialNumber, Logger_)) {
				Logger_.error(fmt::format("Invalid configuration for device {}: wrong mesh config.", ap.serialNumber));
				return false;
			}
			auto NewConfig = ConfigObj->getObject("configuration");
			auto interfaces = NewConfig->getArray("interfaces");
			std::string NewSsid = DEFAULT_SSID_PREFIX + ap.macAddress.substr(MAC_SUFFIX_START_INDEX);
			std::string NewPassword = Poco::toUpper(ap.macAddress);
			Logger_.information(fmt::format("Preparing default configuration for device {}  with SSID '{}' and Password '{}'.",
											ap.serialNumber, NewSsid, NewPassword));
			//  Update SSID and Password
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
		return PrepareProvSubDeviceConfig(NewConfig, subDevice.configuration);
	}

	bool ConfigMaker::CreateSubDeviceInfo(const ProvObjects::InventoryTag &inventoryTag, const SecurityObjects::UserInfo &userInfo,
										  ProvObjects::SubscriberDevice &device) {
		device.info.name = inventoryTag.serialNumber;
		device.serialNumber = inventoryTag.serialNumber;
		device.deviceType = inventoryTag.deviceType;
		device.operatorId = userInfo.owner;
		device.subscriberId = userInfo.id;
		device.realMacAddress = inventoryTag.serialNumber;
		device.deviceRules = inventoryTag.deviceRules;
		ProvObjects::CreateObjectInfo(userInfo, device.info);
		return true;
	}
} // namespace OpenWifi
