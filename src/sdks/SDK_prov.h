/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#pragma once

#include <optional>

#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "framework/RESTAPI_Handler.h"

namespace OpenWifi::SDK::Prov {

	namespace Device {
		// Get a single inventory tag for the given MAC/serial.
		bool Get(RESTAPIHandler *client, const std::string &Mac,
				 ProvObjects::InventoryTag &Device);
		bool UpdateInventoryVenue(RESTAPIHandler *client, const std::string &Mac, const std::string &venueId,
						 ProvObjects::InventoryTag &Device);
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

	namespace Venue {
		bool Get(RESTAPIHandler *client, const std::string &VenueUUID, ProvObjects::Venue &Venue,
				 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
				 Poco::JSON::Object::Ptr &CallResponse);
	} // namespace Venue

	namespace Subscriber {
		bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &OperatorId, ProvObjects::SubscriberDeviceList &devList,
						Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse);
		bool SetSubscriber(RESTAPIHandler *client, const std::string &SubscriberId,
									 const std::string &SerialNumber, bool removeSubscriber = false);
		bool CreateSubsciberDevice(RESTAPIHandler *client, ProvObjects::SubscriberDevice &device);
		bool UpdateSubscriberDevice(RESTAPIHandler *client, const ProvObjects::SubscriberDevice &D);
		bool GetSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber,
					   ProvObjects::SubscriberDevice &D);
		bool DeleteSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber);
		bool DeleteSubscriberVenue(RESTAPIHandler *client, const std::string &subscriberId,
									   Poco::Net::HTTPServerResponse::HTTPStatus &callStatus);
		bool CreateSubscriberVenue(RESTAPIHandler *client, const std::string &subscriberId,
								 bool enableMonitoring, const std::optional<uint64_t> &retention,
								 const std::optional<uint64_t> &interval,
								 const std::optional<bool> &monitorSubVenues,
								 Poco::Net::HTTPServerResponse::HTTPStatus &callStatus,
								 Poco::JSON::Object::Ptr &callResponse);
	} // namespace Subscriber

	namespace Signup {
		bool GetSignupDevice(RESTAPIHandler *client, const std::string &macAddress,
							 Poco::JSON::Object::Ptr &response);
		bool UpdateSignupDevice(RESTAPIHandler *client, const std::string &userId, const std::string &macAddress);
	} // namespace Signup

} // namespace OpenWifi::SDK::Prov
