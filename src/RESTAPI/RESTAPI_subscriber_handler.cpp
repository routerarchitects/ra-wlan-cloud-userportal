//
// Created by stephane bourque on 2021-11-07.
//

#include "RESTAPI_subscriber_handler.h"
#include "StorageService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"

namespace OpenWifi {
    void RESTAPI_subscriber_handler::DoGet() {

        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id,SI)) {
            Poco::JSON::Object  Answer;
            SI.to_json(Answer);
            return ReturnObject(Answer);
        }

        auto Now = std::time(nullptr);

        //  ok, we need to generate a default record and store it...
        SI.id = UserInfo_.userinfo.Id;
        SI.created = SI.modified = Now;
        SI.userId = UserInfo_.userinfo.email;
        SubObjects::AccessPoint AP;
        AP.macAddress = "000000000000";
        AP.id = MicroService::instance().CreateUUID();
        AP.name = "My First Access Point";
        AP.deviceMode.created = AP.deviceMode.modified = Now;
        AP.deviceMode.type = "nat" ;
        AP.deviceMode.enableLEDS = true;
        AP.internetConnection.modified = AP.internetConnection.created = Now;
        AP.internetConnection.type = "automatic";

        SubObjects::WifiNetwork WN;
        WN.type = "main";
        WN.name = "HomeWifi";
        WN.password = "OpenWifi";
        WN.encryption = "wpa2";
        WN.bands.push_back("2G");
        WN.bands.push_back("5G");

        AP.wifiNetworks.created = AP.wifiNetworks.modified = Now;
        AP.wifiNetworks.wifiNetworks.push_back(WN);
        SI.accessPoints.list.push_back(AP);

        StorageService()->SubInfoDB().CreateRecord(SI);

        Poco::JSON::Object  Answer;
        SI.to_json(Answer);

        ReturnObject(Answer);
    }

    void RESTAPI_subscriber_handler::DoPut() {

    }

}