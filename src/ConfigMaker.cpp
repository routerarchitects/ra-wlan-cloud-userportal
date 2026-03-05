/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-12-13.
//

#include "ConfigMaker.h"
#include "Poco/JSON/Parser.h"
#include "Poco/JSON/Stringifier.h"
#include "Poco/String.h"
#include "framework/ow_constants.h"
#include "nlohmann/json.hpp"
#include <fmt/format.h>
#include <exception>
#include <sstream>
#include <utility>
#include <vector>

namespace OpenWifi {
	namespace {
			std::string BuildDefaultSsid(const std::string &deviceMac) {
				const std::string suffix = deviceMac.size() > MAC_SUFFIX_START_INDEX
										   ? deviceMac.substr(MAC_SUFFIX_START_INDEX)
										   : deviceMac;
				return DEFAULT_SSID_PREFIX + suffix;
			}

			/*
			 * This function prepares and updates the provisioning configuration for an
			 * Access Point (AP) using the configuration fetched from the controller
			 * (owgw).
			 *
			 * Example of configuration received from controller:
			 * {
			 *     "interfaces": [ ... ],
			 *     "metrics": { ... },
			 *     "radios": [ ... ],
			 *     "services": { ... },
			 *     "uuid": "some-uuid"
			 * }
			 *
			 * Goal:
			 * We need to convert this controller configuration into a provisioning format
			 * that can be stored in the provisioning database.
			 *
			 * Conversion Rules:
			 * - Keep only sections that are JSON objects or arrays (ignore simple
			 *   key-value pairs).
			 * - For each section (like "interfaces", "metrics", "radios", "services"):
			 *     - Add a "name" (section name)
			 *     - Add a "weight" (default: 1)
			 *     - Store its full JSON data as a configuration element
			 *
			 * Example of resulting provisioning configuration format:
			 * {
			 *     "interfaces": {
			 *         "name": "interfaces",
			 *         "weight": 1,
			 *         ...
			 *     },
			 *     "metrics": {
			 *         "name": "metrics",
			 *         "weight": 1,
			 *         ...
			 *     },
			 *     "radios": {
			 *         "name": "radios",
			 *         "weight": 1,
			 *         ...
			 *     },
			 *     "services": {
			 *         "name": "services",
			 *         "weight": 1,
			 *         ...
			 *     }
			 * }
			 *
			 * In summary:
			 * The function extracts each relevant section (object or array) from the
			 * controller config, adds name and weight to it, and stores it in the
			 * provisioning database for that AP.
			 */
			bool AppendWeightedSection(const Poco::JSON::Object::Ptr &config, const std::string &name,
									   ProvObjects::DeviceConfigurationElementVec &out) {
				Poco::JSON::Object::Ptr wrappedSection = new Poco::JSON::Object();
			if (config->isArray(name)) {
				auto section = config->getArray(name);
				if (!section) {
					return false;
				}
				wrappedSection->set(name, section);
			} else if (config->isObject(name)) {
				auto section = config->getObject(name);
				if (!section) {
					return false;
				}
				wrappedSection->set(name, section);
			} else {
				return true;
			}

			std::ostringstream serialized;
			Poco::JSON::Stringifier::stringify(wrappedSection, serialized);

			ProvObjects::DeviceConfigurationElement element;
			element.name = name;
			element.weight = DEFAULT_DEVICE_CONFIG_WEIGHT;
			element.configuration = serialized.str();
			out.push_back(std::move(element));
			return true;
		}
	} // namespace

	bool ConfigMaker::ValidateConfig(const Poco::JSON::Object::Ptr &deviceConfig,
									 const std::string &serialNumber) {
		try {
			if (!deviceConfig) {
				poco_error(Logger_,
						   fmt::format("Invalid configuration for device {}: empty payload.",
									   serialNumber));
				return false;
			}

			if (!deviceConfig->has("configuration") || !deviceConfig->isObject("configuration")) {
				poco_error(
					Logger_,
					fmt::format("Invalid configuration for device {}: missing configuration block.",
								serialNumber));
				return false;
			}

			auto configuration = deviceConfig->getObject("configuration");
			if (!configuration) {
				poco_error(Logger_,
						   fmt::format("Invalid configuration for device {}: empty configuration.",
									   serialNumber));
				return false;
			}

			if (!configuration->has("interfaces") || !configuration->isArray("interfaces")) {
				poco_error(
					Logger_,
					fmt::format("Invalid configuration for device {}: missing/invalid interfaces.",
								serialNumber));
				return false;
			}

			auto interfaces = configuration->getArray("interfaces");
			if (!interfaces || interfaces->size() == 0) {
				poco_error(Logger_,
						   fmt::format("Invalid configuration for device {}: missing interfaces.",
									   serialNumber));
				return false;
			}

			bool downstreamFound = false;
			for (std::size_t i = 0; i < interfaces->size(); ++i) {
				auto iface = interfaces->getObject(i);
				if (!iface || !iface->has("role") || !iface->get("role").isString()) {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: interface is not an object or "
							"missing/invalid role.",
							serialNumber));
					return false;
				}

				const std::string role = iface->getValue<std::string>("role");
				Poco::JSON::Array::Ptr ssids;
				if (iface->has("ssids") && iface->isArray("ssids")) {
					ssids = iface->getArray("ssids");
				}

				if (role == "upstream") {
					if (ssids && ssids->size() != 0) {
						poco_error(Logger_, fmt::format("Invalid configuration for device {}: "
													   "upstream interface contains SSIDs.",
													   serialNumber));
						return false;
					}
					continue;
				}

				if (role != "downstream") {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: invalid interface role (expected "
							"'upstream' or 'downstream').",
							serialNumber));
					return false;
				}

				downstreamFound = true;
				bool meshSsidFound = false;
				bool apSsidFound = false;

				if (!iface->has("ipv4") || !iface->isObject("ipv4")) {
					poco_error(Logger_, fmt::format("Invalid configuration for device {}: "
												   "downstream interface missing or invalid "
												   "IPv4 value.",
												   serialNumber));
					return false;
				}

				auto ipv4 = iface->getObject("ipv4");
				if (!ipv4->has("addressing") || !ipv4->get("addressing").isString() ||
					ipv4->getValue<std::string>("addressing") != "static") {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: downstream interface should have "
							"static IPv4 addressing.",
							serialNumber));
					return false;
				}

				if (!iface->has("tunnel") || !iface->isObject("tunnel")) {
					poco_error(Logger_, fmt::format("Invalid configuration for device {}: "
												   "downstream interface missing or invalid "
												   "tunnel object.",
												   serialNumber));
					return false;
				}

				auto tunnel = iface->getObject("tunnel");
				if (!tunnel->has("proto") || !tunnel->get("proto").isString() ||
					tunnel->getValue<std::string>("proto") != "mesh") {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: downstream interface must have "
							"tunnel-proto='mesh'.",
							serialNumber));
					return false;
				}

				if (!ssids || ssids->size() == 0) {
					poco_error(Logger_, fmt::format("Invalid configuration for device {}: "
												   "downstream interface missing or invalid "
												   "SSIDs.",
												   serialNumber));
					return false;
				}

				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (!ssid) {
						poco_error(Logger_, fmt::format("Invalid configuration for device {}: "
													   "downstream interface contains a "
													   "non-object SSID entry.",
													   serialNumber));
						return false;
					}

					if (!ssid->has("bss-mode") || !ssid->get("bss-mode").isString()) {
						poco_error(Logger_, fmt::format("Invalid configuration for device {}: SSID "
													   "entry missing or invalid "
													   "'bss-mode'.",
													   serialNumber));
						return false;
					}

					const std::string mode = ssid->getValue<std::string>("bss-mode");
					if (mode == "ap") {
						apSsidFound = true;
					} else if (mode == "mesh") {
						meshSsidFound = true;
					}
				}

				if (!apSsidFound) {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: missing ap-SSID on downstream "
							"interface.",
							serialNumber));
					return false;
				}
				if (!meshSsidFound) {
					poco_error(
						Logger_,
						fmt::format(
							"Invalid configuration for device {}: missing mesh-SSID on downstream "
							"interface.",
							serialNumber));
					return false;
				}
			}

			if (!downstreamFound) {
				poco_error(Logger_,
						   fmt::format(
							   "Invalid configuration for device {}: missing downstream interface.",
							   serialNumber));
				return false;
			}

			return true;
		} catch (...) {
			poco_error(Logger_, fmt::format("ValidateConfig failed for device {}.", serialNumber));
		}
		return false;
	}

	bool ConfigMaker::BuildMeshConfig(const Poco::JSON::Object::Ptr &inputConfig,
									  Poco::JSON::Object::Ptr &outputConfig) {
		try {
			outputConfig = nullptr;
			if (!inputConfig || !inputConfig->has("configuration") ||
				!inputConfig->isObject("configuration")) {
				poco_error(Logger_, "BuildMeshConfig: missing configuration block.");
				return false;
			}

			auto gatewayConfig = inputConfig->getObject("configuration");
			if (!gatewayConfig) {
				poco_error(Logger_, "BuildMeshConfig: empty configuration block.");
				return false;
			}

			std::ostringstream jsonStream;
			Poco::JSON::Stringifier::stringify(gatewayConfig, jsonStream);
			auto cfg = nlohmann::json::parse(jsonStream.str());

			if (cfg.contains("interfaces") && cfg["interfaces"].is_array() &&
				!cfg["interfaces"].empty()) {
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
					if (!cmd.is_array() || cmd.size() < 3) {
						continue;
					}
					if (cmd[0] != "set" || cmd[1] != "firewall.@rule[-1].name" ||
						!cmd[2].is_string()) {
						continue;
					}
					if (cmd[2].get<std::string>() == "Block_Clients") {
						Logger_.information(
							"Removing firewall-rule [Block_Clients] from config-raw for mesh "
							"device.");
						cfg.erase("config-raw");
						break;
					}
				}
			}

			Poco::JSON::Parser parser;
			auto parsed = parser.parse(cfg.dump());
			outputConfig = parsed.extract<Poco::JSON::Object::Ptr>();
			return outputConfig != nullptr;
		} catch (...) {
			poco_error(Logger_, "BuildMeshConfig failed.");
		}
		return false;
	}

	bool
	ConfigMaker::AppendWeightedSections(const Poco::JSON::Object::Ptr &config,
										ProvObjects::DeviceConfigurationElementVec &provConfig) {
		if (!config) {
			return false;
		}

		// Clear existing configuration to prevent duplicate sections in case of retries.
		provConfig.clear();

		std::vector<std::string> sectionNames;
		config->getNames(sectionNames);
		for (const auto &sectionName : sectionNames) {
			if (!(config->isObject(sectionName) || config->isArray(sectionName))) {
				continue;
			}
			if (!AppendWeightedSection(config, sectionName, provConfig)) {
				return false;
			}
		}
		return true;
	}

	bool ConfigMaker::BuildGatewayConfig(const Poco::JSON::Object::Ptr &deviceConfig,
										 const std::string &deviceMac,
										 ProvObjects::SubscriberDevice &subDevice) {
		try {

			if (!ValidateConfig(deviceConfig, deviceMac)) {
				poco_error(Logger_,
						   fmt::format("BuildGatewayConfig: invalid source config for device {}.",
									   deviceMac));
				return false;
			}

			auto newConfig = deviceConfig->getObject("configuration");
			auto interfaces = newConfig->getArray("interfaces");
			std::string newSsid = BuildDefaultSsid(deviceMac);
			std::string newPassword = Poco::toUpper(deviceMac);
			Logger_.information(fmt::format(
				"Preparing default configuration for device {} with SSID '{}' and Password '{}'.",
				deviceMac, newSsid, newPassword));

			for (std::size_t i = 0; i < interfaces->size(); ++i) {
				auto iface = interfaces->getObject(i);
				if (!iface || !iface->has("ssids") || !iface->isArray("ssids")) {
					continue;
				}

				auto ssids = iface->getArray("ssids");
				if (!ssids) {
					continue;
				}

				for (std::size_t j = 0; j < ssids->size(); ++j) {
					auto ssid = ssids->getObject(j);
					if (!ssid || !ssid->has("bss-mode") || !ssid->get("bss-mode").isString() ||
						!ssid->has("encryption") || !ssid->isObject("encryption")) {
						continue;
					}

					const std::string bssMode = ssid->getValue<std::string>("bss-mode");
					auto encryption = ssid->getObject("encryption");
					if (!encryption) {
						continue;
					}

					if (bssMode == "ap") {
						ssid->set("name", newSsid);
						encryption->set("key", newPassword);
					} else if (bssMode == "mesh") {
						encryption->set("key", newPassword);
					}
				}
			}

			return AppendWeightedSections(newConfig, subDevice.configuration);
		} catch (...) {
			poco_error(Logger_, fmt::format("BuildGatewayConfig failed for device {}.", deviceMac));
		}
		return false;
	}
} // namespace OpenWifi
