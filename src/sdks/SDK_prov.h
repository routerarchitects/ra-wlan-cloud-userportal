/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#pragma once

#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "framework/RESTAPI_Handler.h"

namespace OpenWifi::SDK::Prov {

	namespace Device {
		bool Get(RESTAPIHandler *client, const std::string &Mac,
				 ProvObjects::InventoryTagList &Device);
		bool SetConfiguration(RESTAPIHandler *client, const std::string &Mac,
							  const std::string &ConfigUUID);
	} // namespace Device

	namespace Configuration {
		bool Get(RESTAPIHandler *client, const std::string &ConfigUUID,
				 ProvObjects::DeviceConfiguration &Config);
		bool Delete(RESTAPIHandler *client, const std::string &ConfigUUID);
		bool Create(RESTAPIHandler *client, const std::string &SerialNumber,
					const ProvObjects::DeviceConfiguration &Config, std::string &ConfigUUID);
		bool Update(RESTAPIHandler *client, const std::string &ConfigUUID,
					ProvObjects::DeviceConfiguration &Config);
		bool Push(RESTAPIHandler *client, const std::string &serialNumber,
				  ProvObjects::InventoryConfigApplyResult &Results);
	} // namespace Configuration

	namespace Subscriber {
		bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &OperatorId, ProvObjects::SubscriberDeviceList &devList);
		bool UpdateSubscriber(RESTAPIHandler *client, const std::string &SubscriberId,
									 const std::string &SerialNumber, bool removeSubscriber = false);
		bool SetDevice(RESTAPIHandler *client, const ProvObjects::SubscriberDevice &D);
		bool GetDevice(RESTAPIHandler *client, const std::string &SerialNumber,
					   ProvObjects::SubscriberDevice &D);
	} // namespace Subscriber

} // namespace OpenWifi::SDK::Prov
