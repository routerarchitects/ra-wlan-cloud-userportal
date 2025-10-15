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
		bool Prepare();
		bool Push();
		bool DefConfig(const SubObjects::SubscriberInfo &SI);

	  private:
		Poco::Logger &Logger_;
		const std::string id_;
		bool bad_ = false;
	};
} // namespace OpenWifi
