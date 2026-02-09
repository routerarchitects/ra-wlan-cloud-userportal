/*
SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
Copyright (c) 2025 Infernet Systems Pvt Ltd
Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
*/

//
// Created by stephane bourque on 2021-12-13.
//

#pragma once

#include "Poco/Logger.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "RESTObjects/RESTAPI_SubObjects.h" // for SubObjects::SubscriberInfo

namespace OpenWifi {
	class ConfigMaker {
	  public:
		explicit ConfigMaker(Poco::Logger &L, const std::string &Id) : Logger_(L), id_(Id) {}
		bool PrepareDefaultConfig(const SubObjects::AccessPoint &ap, ProvObjects::SubscriberDevice &subDevice);
		bool PrepareProvSubDeviceConfig(const Poco::JSON::Object::Ptr &Config, ProvObjects::DeviceConfigurationElementVec &ProvConfig);
		bool ValidateConfig(const Poco::JSON::Object::Ptr &deviceConfig, const std::string &serialNumber, Poco::Logger &logger);
		bool BuildMeshConfig(const Poco::JSON::Object::Ptr &InputConfig, Poco::JSON::Object::Ptr &OutputConfig);
		bool CreateSubDeviceInfo(const ProvObjects::InventoryTag &inventoryTag, const SecurityObjects::UserInfo &userInfo,
										ProvObjects::SubscriberDevice &device);

	  private:
		Poco::Logger &Logger_;
		const std::string id_;
		bool bad_ = false;
	};
} // namespace OpenWifi
