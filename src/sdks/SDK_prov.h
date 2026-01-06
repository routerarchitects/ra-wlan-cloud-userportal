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
		// Get a single inventory tag for the given MAC/serial.
		bool Get(RESTAPIHandler *client, const std::string &Mac,
				 ProvObjects::InventoryTag &Device);
		bool SetConfiguration(RESTAPIHandler *client, const std::string &Mac,
							  const std::string &ConfigUUID);
		bool DeleteInventoryDevice(RESTAPIHandler *client, const std::string &SerialNumber);
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
		Poco::JSON::Object::Ptr BuildMeshConfig(const Poco::JSON::Object::Ptr &configuration);
		bool CreateSubDeviceInfo(RESTAPIHandler *client, const ProvObjects::InventoryTag &inventoryTag, const SecurityObjects::UserInfo &userInfo,
					   ProvObjects::SubscriberDevice &device);
		bool CreateDevice(RESTAPIHandler *client, ProvObjects::SubscriberDevice &device);
		bool SetDevice(RESTAPIHandler *client, const ProvObjects::SubscriberDevice &D);
		bool GetDevice(RESTAPIHandler *client, const std::string &SerialNumber,
					   ProvObjects::SubscriberDevice &D);
		bool DeleteProvSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber);
	} // namespace Subscriber

	namespace Signup {
		bool Update_Signup_Device(RESTAPIHandler *client, const std::string &userId, const std::string &macAddress);
	} // namespace Signup

} // namespace OpenWifi::SDK::Prov
