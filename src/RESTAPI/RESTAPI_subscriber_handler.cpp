//
// Created by stephane bourque on 2021-11-07.
//

#include "RESTAPI_subscriber_handler.h"
#include "StorageService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"

namespace OpenWifi {

    static void CreateDefaultSubscriberInfo(const SecurityObjects::UserInfoAndPolicy & UI, SubObjects::SubscriberInfo &SI) {
        auto Now = std::time(nullptr);

        //  ok, we need to generate a default record and store it...
        SI.id = UI.userinfo.Id;
        SI.created = SI.modified = Now;
        SI.userId = UI.userinfo.email;
        const auto NameParts = Poco::StringTokenizer(UI.userinfo.name," ");
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
        WN.bands.emplace_back("2G");
        WN.bands.emplace_back("5G");

        AP.wifiNetworks.created = AP.wifiNetworks.modified = Now;
        AP.wifiNetworks.wifiNetworks.push_back(WN);
        SI.accessPoints.list.push_back(AP);
    }

    void RESTAPI_subscriber_handler::DoGet() {

        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id,SI)) {
            Poco::JSON::Object  Answer;
            SI.to_json(Answer);
            return ReturnObject(Answer);
        }

        CreateDefaultSubscriberInfo(UserInfo_, SI);
        StorageService()->SubInfoDB().CreateRecord(SI);

        Poco::JSON::Object  Answer;
        SI.to_json(Answer);
        ReturnObject(Answer);
    }

    void RESTAPI_subscriber_handler::DoPut() {

        SubObjects::SubscriberInfo  Existing;
        if(!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id, Existing)) {
            CreateDefaultSubscriberInfo(UserInfo_, Existing);
        }

        auto Body = ParseStream();
        SubObjects::SubscriberInfo  Changes;
        if(!Changes.from_json(Body)) {
            return BadRequest(RESTAPI::Errors::InvalidJSONDocument);
        }

        auto Now = std::time(nullptr);
        if(Body->has("firstName"))
            Existing.firstName = Changes.firstName;
        if(Body->has("initials"))
            Existing.initials = Changes.initials;
        if(Body->has("lastName"))
            Existing.lastName = Changes.lastName;
        if(Body->has("secondaryEmail") && Utils::ValidEMailAddress(Changes.secondaryEmail))
            Existing.secondaryEmail = Changes.secondaryEmail;
        if(Body->has("serviceAddress"))
            Existing.serviceAddress = Changes.serviceAddress;
        if(Body->has("billingAddress"))
            Existing.billingAddress = Changes.billingAddress;
        if(Body->has("phoneNumber"))
            Existing.phoneNumber = Changes.phoneNumber;
        Existing.modified = Now;

        //  Look at the access points
        for(const auto &New:Changes.accessPoints.list) {
            //  match this ID with the existing ones, if not, we need to add it..
            //  We cannot add a serial of all 0, and we cannot add a device that's already been claimed.
            for(auto &Old:Existing.accessPoints.list) {
                if(New.id==Old.id) {
                    Old = New;
                    Old.internetConnection.modified = Now;
                    Old.deviceMode.modified = Now;
                    Old.wifiNetworks.modified = Now;
                    Old.subscriberDevices.modified = Now;
                }
            }
        }

        if(StorageService()->SubInfoDB().UpdateRecord("id",UserInfo_.userinfo.Id, Existing)) {
            SubObjects::SubscriberInfo  Modified;
            StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.Id,Modified);
            Poco::JSON::Object  Answer;

            Modified.to_json(Answer);
            return ReturnObject(Answer);
        }

        return InternalError("Profile could not be updated. Try again.");
    }

}