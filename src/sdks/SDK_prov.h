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
	} // namespace Device

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
		bool CreateSubsciberDeviceMinimal(
			RESTAPIHandler *client, const std::string &name, const std::string &serialNumber,
			const std::string &subscriberId, const std::string &deviceGroup,
			const ProvObjects::DeviceConfigurationElementVec &configuration,
			ProvObjects::SubscriberDevice &device);
		bool DeleteSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber,
									Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus);
	} // namespace Subscriber

} // namespace OpenWifi::SDK::Prov
