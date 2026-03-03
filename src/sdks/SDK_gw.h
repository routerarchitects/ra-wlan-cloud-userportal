/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#pragma once

#include "RESTObjects/RESTAPI_SubObjects.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "RESTObjects/RESTAPI_GWobjects.h"
#include "framework/RESTAPI_Handler.h"

namespace OpenWifi::SDK::GW {
	namespace Device {
		bool Reboot(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					Poco::JSON::Object::Ptr &Response);
		bool LEDs(RESTAPIHandler *client, const std::string &Mac, uint64_t When, uint64_t Duration,
				  const std::string &Pattern,
				  Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
				  Poco::JSON::Object::Ptr &Response);
		bool Factory(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 bool KeepRedirector,
					 Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					 Poco::JSON::Object::Ptr &Response);
		bool Upgrade(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 const std::string &ImageName, bool KeepRedirector,
					 Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					 Poco::JSON::Object::Ptr &Response);
		bool Configure(RESTAPIHandler *client, const std::string &Mac,
					   Poco::JSON::Object::Ptr &Configuration,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response);
		bool GetConfig(RESTAPIHandler *client, const std::string &Mac,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response);
		bool SetConfig(RESTAPIHandler *client, const Poco::JSON::Object::Ptr &Body,
					   const ProvObjects::SubscriberDeviceList &SubscriberDevices,
					   const std::string &GatewaySerial,
					   Poco::Net::HTTPResponse::HTTPStatus &ResponseStatus,
					   Poco::JSON::Object::Ptr &Response);
		bool GetBlockedClients(const Poco::JSON::Object::Ptr &Config, std::list<std::string> &blockedMacs);
		bool GetLastStats(RESTAPIHandler *client, const std::string &Mac,
						  Poco::JSON::Object::Ptr &Response);
		bool GetOUIs(RESTAPIHandler *client, Types::StringPairVec &MacList);
	} // namespace Device
} // namespace OpenWifi::SDK::GW
