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

    void SubscriberInfoDB::CreateDefaultSubscriberInfo(const SecurityObjects::UserInfoAndPolicy & UI, SubObjects::SubscriberInfo &SI, const ProvObjects::InventoryTagList & Devices) {
        auto Now = std::time(nullptr);

        //  ok, we need to generate a default record and store it...
        SI.id = UI.userinfo.id;
        SI.created = SI.modified = Now;
        SI.userId = UI.userinfo.email;
        const auto NameParts = Poco::StringTokenizer(UI.userinfo.name.empty() ? SI.userId : UI.userinfo.name," @._-");
        if(NameParts.count()>0) {
            for(auto i=0; i<NameParts.count();++i) {
                if(i==0)
                    SI.firstName = NameParts[0];
                else
                    SI.lastName = NameParts[i] + " ";
            }
            Poco::trimInPlace(SI.firstName);
            Poco::trimInPlace(SI.lastName);
        } else {
            SI.firstName = UI.userinfo.name;
        }

        if(!UI.userinfo.userTypeProprietaryInfo.mobiles.empty())
            SI.phoneNumber = UI.userinfo.userTypeProprietaryInfo.mobiles[0].number;

        int ap_num=1;
        for(const auto &i:Devices.taglist) {
            SubObjects::AccessPoint AP;
            AP.macAddress = i.serialNumber;
            AP.deviceType = i.deviceType;
            AP.id = i.info.id;
            AP.name = "Access Point #" + std::to_string(ap_num++);
            AP.deviceMode.created = AP.deviceMode.modified = Now;
            AP.deviceMode.type = "nat";
            AP.deviceMode.enableLEDS = true;
            AP.internetConnection.modified = AP.internetConnection.created = Now;
            AP.internetConnection.type = "automatic";

            SubObjects::WifiNetwork WN;
            WN.type = "main";
            WN.name = "OpenWifi-" + AP.macAddress.substr(6);
            WN.password = Poco::toUpper(i.serialNumber);
            WN.encryption = "wpa2-personal";
            if(AP.deviceType=="linksys_ea8300") {
                WN.bands.emplace_back("2G");
                WN.bands.emplace_back("5GL");
                WN.bands.emplace_back("5GU");
            } else {
                WN.bands.emplace_back("2G");
                WN.bands.emplace_back("5G");
            }

            AP.wifiNetworks.created = AP.wifiNetworks.modified = Now;
            AP.wifiNetworks.wifiNetworks.push_back(WN);

            for(const auto &b:WN.bands) {
                SubObjects::RadioInformation    RI;

                RI.band = b;
                RI.rates.beacon = 6000;
                RI.rates.multicast = 24000;
                RI.channel = 0;
                RI.country = i.locale;
                RI.maximumClients = 64;
                RI.legacyRates = false;
                RI.he.bssColor = 1;
                RI.he.ema = false;
                RI.he.multipleBSSID = false;
                RI.txpower = 0 ;
                RI.beaconInterval = 100;
                RI.dtimPeriod = 2;
                RI.allowDFS = false;
                RI.mimo = "2x2";
                if(b=="2G") {
                    RI.bandwidth = 20;
                    RI.channelWidth = 40 ;
                    RI.channelMode = "HT";
                    RI.requireMode = "HT";
                } else if(b=="5G") {
                    RI.bandwidth = 20;
                    RI.channelWidth = 40 ;
                    RI.channelMode = "HE";
                    RI.requireMode = "HT";
                } else if(b=="5GU") {
                    RI.bandwidth = 20;
                    RI.channelWidth = 40 ;
                    RI.channelMode = "HE";
                    RI.requireMode = "HT";
                } else if(b=="5GL") {
                    RI.bandwidth = 20;
                    RI.channelWidth = 40 ;
                    RI.channelMode = "HE";
                    RI.requireMode = "HT";
                } else if(b=="6G") {
                    RI.bandwidth = 20;
                    RI.channelWidth = 40 ;
                    RI.channelMode = "HE";
                    RI.requireMode = "HT";
                }
                AP.radios.emplace_back(RI);
            }
            SI.accessPoints.list.push_back(AP);
        }
    }
}

template<> void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(const OpenWifi::SubInfoDBRecordType &In, OpenWifi::SubObjects::SubscriberInfo &Out) {
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

template<> void ORM::DB<OpenWifi::SubInfoDBRecordType, OpenWifi::SubObjects::SubscriberInfo>::Convert(const OpenWifi::SubObjects::SubscriberInfo &In, OpenWifi::SubInfoDBRecordType &Out) {
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
