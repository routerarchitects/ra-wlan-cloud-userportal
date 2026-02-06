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
			if (!SDK::GW::Device::ValidateConfig(ConfigObj, ap.serialNumber, Logger_)) {
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
} // namespace OpenWifi
