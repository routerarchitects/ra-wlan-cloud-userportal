/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-11-29.
//

#include "storage_subscriber_info.h"
#include "framework/RESTAPI_utils.h"
#include "framework/utils.h"

namespace OpenWifi {

	static ORM::FieldVec SubInfoDBDB_Fields{// object info
											ORM::Field{"id", 64, true},
											ORM::Field{"userId", ORM::FieldType::FT_TEXT},
											ORM::Field{"firstName", ORM::FieldType::FT_TEXT},
											ORM::Field{"initials", ORM::FieldType::FT_TEXT},
											ORM::Field{"lastName", ORM::FieldType::FT_TEXT},
											ORM::Field{"phoneNumber", ORM::FieldType::FT_TEXT},
											ORM::Field{"secondaryEmail", ORM::FieldType::FT_TEXT},
											ORM::Field{"accessPoints", ORM::FieldType::FT_TEXT},
											ORM::Field{"serviceAddress", ORM::FieldType::FT_TEXT},
											ORM::Field{"billingAddress", ORM::FieldType::FT_TEXT},
											ORM::Field{"created", ORM::FieldType::FT_BIGINT},
											ORM::Field{"modified", ORM::FieldType::FT_BIGINT}};

	static ORM::IndexVec SubInfoDBDB_Fields_Indexes{
		{std::string("subinfo_userid_index"),
		 ORM::IndexEntryVec{{std::string("userId"), ORM::Indextype::ASC}}},
		{std::string("subinfo_phone_index"),
		 ORM::IndexEntryVec{{std::string("phoneNumber"), ORM::Indextype::ASC}}}};

	SubscriberInfoDB::SubscriberInfoDB(OpenWifi::DBType T, Poco::Data::SessionPool &P,
									   Poco::Logger &L)
		: DB(T, "subscriberinfo", SubInfoDBDB_Fields, SubInfoDBDB_Fields_Indexes, P, L, "sui") {}

	void SubscriberInfoDB::CreateDefaultSubscriberInfo(
		const SecurityObjects::UserInfoAndPolicy &UI, SubObjects::SubscriberInfo &SI,
		const ProvObjects::SubscriberDeviceList &Devices) {
		auto Now = Utils::Now();

		//  ok, we need to generate a default record and store it...
		SI.id = UI.userinfo.id;
		SI.created = SI.modified = Now;
		SI.userId = UI.userinfo.email;
		const auto NameParts =
			Poco::StringTokenizer(UI.userinfo.name.empty() ? SI.userId : UI.userinfo.name, " @._-");
		if (NameParts.count() > 0) {
			for (size_t i = 0; i < NameParts.count(); ++i) {
				if (i == 0)
					SI.firstName = NameParts[0];
				else
					SI.lastName = NameParts[i] + " ";
			}
			Poco::trimInPlace(SI.firstName);
			Poco::trimInPlace(SI.lastName);
		} else {
			SI.firstName = UI.userinfo.name;
		}

		if (!UI.userinfo.userTypeProprietaryInfo.mobiles.empty())
			SI.phoneNumber = UI.userinfo.userTypeProprietaryInfo.mobiles[0].number;

		// Populate the subscriber's access point list with provisioned subdevices
		for (const auto &Device : Devices.subscriberDevices) {
			AddAccessPoint(SI, Device.serialNumber, Device.deviceType, Device);
		}
	}

	void SubscriberInfoDB::AddAccessPoint(SubObjects::SubscriberInfo &SI, const std::string &macAddress,
										  const std::string &deviceType, const ProvObjects::SubscriberDevice &ProvisionedDevice) {
		SubObjects::AccessPoint AP;
		AP.name = "Access Point #" + std::to_string(SI.accessPoints.list.size() + 1);
		AP.macAddress = macAddress;
		AP.serialNumber = macAddress;
		AP.id = ProvisionedDevice.info.id;
		AP.deviceType = deviceType;
		SI.accessPoints.list.push_back(AP);
	}
} // namespace OpenWifi

template <>
void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(
	const OpenWifi::SubInfoDBRecordType &In, OpenWifi::SubObjects::SubscriberInfo &Out) {
	Out.id = In.get<0>();
	Out.userId = In.get<1>();
	Out.firstName = In.get<2>();
	Out.initials = In.get<3>();
	Out.lastName = In.get<4>();
	Out.phoneNumber = In.get<5>();
	Out.secondaryEmail = In.get<6>();
	Out.accessPoints =
		OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::AccessPointList>(In.get<7>());
	Out.serviceAddress =
		OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::Location>(In.get<8>());
	Out.billingAddress =
		OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::Location>(In.get<9>());
	Out.created = In.get<10>();
	Out.modified = In.get<11>();
}

template <>
void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(
	const OpenWifi::SubObjects::SubscriberInfo &In, OpenWifi::SubInfoDBRecordType &Out) {
	Out.set<0>(In.id);
	Out.set<1>(In.userId);
	Out.set<2>(In.firstName);
	Out.set<3>(In.initials);
	Out.set<4>(In.lastName);
	Out.set<5>(In.phoneNumber);
	Out.set<6>(In.secondaryEmail);
	Out.set<7>(OpenWifi::RESTAPI_utils::to_string(In.accessPoints));
	Out.set<8>(OpenWifi::RESTAPI_utils::to_string(In.serviceAddress));
	Out.set<9>(OpenWifi::RESTAPI_utils::to_string(In.billingAddress));
	Out.set<10>(In.created);
	Out.set<11>(In.modified);
}
