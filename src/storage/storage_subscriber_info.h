//
// Created by stephane bourque on 2021-11-29.
//

#pragma once

#include "framework/orm.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {
    typedef Poco::Tuple<
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                std::string,
                uint64_t,
                uint64_t
            > SubInfoDBRecordType;

    typedef std::vector<SubInfoDBRecordType> SubInfoDBRecordList;

    class SubscriberInfoDB : public ORM::DB<SubInfoDBRecordType, SubObjects::SubscriberInfo> {
    public:
        SubscriberInfoDB( OpenWifi::DBType T, Poco::Data::SessionPool & P, Poco::Logger &L);
        virtual ~SubscriberInfoDB() {};
        void CreateDefaultSubscriberInfo(const SecurityObjects::UserInfoAndPolicy & UI, SubObjects::SubscriberInfo &SI,const ProvObjects::InventoryTagList &Devices);
    private:
    };
}