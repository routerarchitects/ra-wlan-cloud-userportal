/*
SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
Copyright (c) 2025 Infernet Systems Pvt Ltd
Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
*/

//
// Created by stephane bourque on 2021-12-13.
//

#pragma once

#include "Poco/JSON/Object.h"
#include "Poco/Logger.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {
	class ConfigMaker {
	  public:
		explicit ConfigMaker(Poco::Logger &L) : Logger_(L) {}

		bool ValidateConfig(const Poco::JSON::Object::Ptr &deviceConfig,
							const std::string &serialNumber);
		bool BuildMeshConfig(const Poco::JSON::Object::Ptr &inputConfig,
							 Poco::JSON::Object::Ptr &outputConfig);
		bool BuildGatewayConfig(const Poco::JSON::Object::Ptr &deviceConfig,
								const std::string &deviceMac,
								ProvObjects::SubscriberDevice &subDevice);
		bool AppendWeightedSections(const Poco::JSON::Object::Ptr &config,
									ProvObjects::DeviceConfigurationElementVec &provConfig);

	  private:
		Poco::Logger &Logger_;
	};
} // namespace OpenWifi
