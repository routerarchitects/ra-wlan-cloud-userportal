//
// Created by stephane bourque on 2021-11-07.
//

#include "RESTAPI_subscriber_handler.h"
#include "StorageService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "SubscriberCache.h"

namespace OpenWifi {

    void RESTAPI_subscriber_handler::DoGet() {

        if(UserInfo_.userinfo.Id.empty()) {
            return NotFound();
        }

        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id,SI)) {
            Poco::JSON::Object  Answer;
            SI.to_json(Answer);
            return ReturnObject(Answer);
        }

        StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI);
        StorageService()->SubInfoDB().CreateRecord(SI);

        Poco::JSON::Object  Answer;
        SI.to_json(Answer);
        ReturnObject(Answer);
    }

    void RESTAPI_subscriber_handler::DoPut() {

        if(UserInfo_.userinfo.Id.empty()) {
            return NotFound();
        }

        SubObjects::SubscriberInfo  Existing;
        if(!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id, Existing)) {
            StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, Existing);
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
                    if (Old.macAddress == "000000000000") {
                        Old = New;
                    } else {
                        auto T = Old.macAddress;
                        Old = New;
                        Old.macAddress = T;
                    }
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
            SubscriberCache()->UpdateSubInfo(UserInfo_.userinfo.Id,Modified);
            Poco::JSON::Object  Answer;

            Modified.to_json(Answer);
            return ReturnObject(Answer);
        }

        return InternalError("Profile could not be updated. Try again.");
    }

}