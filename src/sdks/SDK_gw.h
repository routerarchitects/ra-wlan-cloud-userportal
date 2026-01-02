/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#pragma once

#include "RESTObjects/RESTAPI_GWobjects.h"
#include "framework/RESTAPI_Handler.h"

namespace OpenWifi::SDK::GW {
	namespace Device {
		void Reboot(RESTAPIHandler *client, const std::string &Mac, uint64_t When);
		void LEDs(RESTAPIHandler *client, const std::string &Mac, uint64_t When, uint64_t Duration,
				  const std::string &Pattern);
		void Factory(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 bool KeepRedirector);
		void Upgrade(RESTAPIHandler *client, const std::string &Mac, uint64_t When,
					 const std::string &ImageName, bool KeepRedirector);
		void PerformCommand(RESTAPIHandler *client, const std::string &Command,
							const std::string &EndPoint, Poco::JSON::Object &CommandRequest);
		bool Configure(RESTAPIHandler *client, const std::string &Mac,
					   Poco::JSON::Object::Ptr &Configuration, Poco::JSON::Object::Ptr &Response);
		Poco::Net::HTTPResponse::HTTPStatus GetConfig(RESTAPIHandler *client, const std::string &Mac,
					Poco::JSON::Object::Ptr &Response);
		bool ValidateMeshSSID(const Poco::JSON::Object::Ptr &deviceConfig, const std::string &serialNumber, Poco::Logger &logger);
		Poco::Net::HTTPResponse::HTTPStatus SetConfig(RESTAPIHandler *client, const std::string &SerialNumber,
				   const Poco::JSON::Object::Ptr &Body, std::string &status);
		bool SetVenue(RESTAPIHandler *client, const std::string &SerialNumber,
					  const std::string &uuid);
		bool GetLastStats(RESTAPIHandler *client, const std::string &Mac,
						  Poco::JSON::Object::Ptr &Response);
		bool SetSubscriber(RESTAPIHandler *client, const std::string &SerialNumber,
						   const std::string &uuid);
		bool GetOUIs(RESTAPIHandler *client, Types::StringPairVec &MacList);
		bool DeleteOwgwDevice(RESTAPIHandler *client, const std::string &SerialNumber);
	} // namespace Device
} // namespace OpenWifi::SDK::GW
