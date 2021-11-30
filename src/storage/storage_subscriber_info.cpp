//
// Created by stephane bourque on 2021-11-29.
//

#include "storage_subscriber_info.h"

namespace OpenWifi {

    static  ORM::FieldVec    SubInfoDBDB_Fields{
        // object info
        ORM::Field{"id",64, true},
        ORM::Field{"userId",ORM::FieldType::FT_TEXT},
        ORM::Field{"firstName",ORM::FieldType::FT_TEXT},
        ORM::Field{"initials",ORM::FieldType::FT_TEXT},
        ORM::Field{"lastName",ORM::FieldType::FT_TEXT},
        ORM::Field{"phoneNumber",ORM::FieldType::FT_TEXT},
        ORM::Field{"secondaryEmail",ORM::FieldType::FT_TEXT},
        ORM::Field{"accessPoints",ORM::FieldType::FT_TEXT},
        ORM::Field{"serviceAddress",ORM::FieldType::FT_TEXT},
        ORM::Field{"billingAddress",ORM::FieldType::FT_TEXT},
        ORM::Field{"created",ORM::FieldType::FT_BIGINT},
        ORM::Field{"modified",ORM::FieldType::FT_BIGINT}
    };

    static  ORM::IndexVec    SubInfoDBDB_Fields_Indexes{
        { std::string("subinfo_userid_index"),
          ORM::IndexEntryVec{
            {std::string("userId"),
             ORM::Indextype::ASC} } },
         { std::string("subinfo_phone_index"),
           ORM::IndexEntryVec{
             {std::string("phoneNumber"),
              ORM::Indextype::ASC} } }
    };

    SubscriberInfoDB::SubscriberInfoDB( OpenWifi::DBType T, Poco::Data::SessionPool & P, Poco::Logger &L) :
        DB(T, "subscriberinfo", SubInfoDBDB_Fields, SubInfoDBDB_Fields_Indexes, P, L, "sui") {}

}

template<> void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(OpenWifi::SubInfoDBRecordType &In, OpenWifi::SubObjects::SubscriberInfo &Out) {
    Out.id = In.get<0>();
    Out.userId = In.get<1>();
    Out.firstName = In.get<2>();
    Out.initials = In.get<3>();
    Out.lastName = In.get<4>();
    Out.phoneNumber = In.get<5>();
    Out.secondaryEmail = In.get<6>();
    Out.accessPoints = OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::AccessPointList>(In.get<7>());
    Out.serviceAddress = OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::Location>(In.get<8>());
    Out.billingAddress = OpenWifi::RESTAPI_utils::to_object<OpenWifi::SubObjects::Location>(In.get<9>());
    Out.created = In.get<10>();
    Out.modified = In.get<11>();
}

template<> void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(OpenWifi::SubObjects::SubscriberInfo &In, OpenWifi::SubInfoDBRecordType &Out) {
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
