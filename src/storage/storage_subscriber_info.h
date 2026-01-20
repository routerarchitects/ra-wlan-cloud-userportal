/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
//
// Created by stephane bourque on 2021-11-29.
//

#pragma once

#include "RESTObjects/RESTAPI_ProvObjects.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "framework/orm.h"

namespace OpenWifi {
	typedef Poco::Tuple<std::string, std::string, std::string, std::string, std::string,
						std::string, std::string, std::string, std::string, std::string, uint64_t,
						uint64_t>
		SubInfoDBRecordType;

	typedef std::vector<SubInfoDBRecordType> SubInfoDBRecordList;

	class SubscriberInfoDB : public ORM::DB<SubInfoDBRecordType, SubObjects::SubscriberInfo> {
	  public:
		SubscriberInfoDB(OpenWifi::DBType T, Poco::Data::SessionPool &P, Poco::Logger &L);
		virtual ~SubscriberInfoDB(){};
		void BuildDefaultSubscriberInfo(const SecurityObjects::UserInfoAndPolicy &UI,
										 SubObjects::SubscriberInfo &SI,
										 const ProvObjects::SubscriberDeviceList &Devices);
		void AddAccessPoint(SubObjects::SubscriberInfo &SI, const std::string &macAddress,
							const std::string &deviceType, const ProvObjects::SubscriberDevice &ProvisionedDevice);

	  private:
	};
} // namespace OpenWifi
