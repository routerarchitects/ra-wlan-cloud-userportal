//
// Created by stephane bourque on 2021-11-07.
//

#include "RESTAPI_subscriber_handler.h"
#include "StorageService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "SubscriberCache.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_gw.h"
#include "ConfigMaker.h"

#define __DBG__ std::cout << __LINE__ << std::endl ;

namespace OpenWifi {

    void RESTAPI_subscriber_handler::DoGet() {

        if(UserInfo_.userinfo.id.empty()) {
            return NotFound();
        }

        std::cout << "Getting default subscriber info: " << UserInfo_.userinfo.id << std::endl;
        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id,SI)) {
            __DBG__
            //  we need to get the stats for each AP
            for(auto &i:SI.accessPoints.list) {
                if(i.macAddress.empty())
                    continue;
                __DBG__
                Poco::JSON::Object::Ptr LastStats;
                if(SDK::GW::Device::GetLastStats(nullptr,i.macAddress,LastStats)) {
                    __DBG__
                    std::ostringstream OS;
                    __DBG__
                    LastStats->stringify(OS);
                    __DBG__
                    try {
                        nlohmann::json LA = nlohmann::json::parse(OS.str());
                        __DBG__
                        for (const auto &j: LA["interfaces"]) {
                            __DBG__
                            if (j.contains("ipv4")) {
                                auto IPparts = Poco::StringTokenizer(to_string(j["ipv4"]["addresses"][0]), "/");
                                i.internetConnection.ipAddress = IPparts[0];
                                i.internetConnection.subnetMask = IPparts[1];
                                i.internetConnection.defaultGateway = to_string(j["ipv4"]["dhcp_server"]);
                            }
                            __DBG__
                            if (j.contains("dns_servers") && j["dns_servers"].is_array()) {
                                auto dns = j["dns_servers"];
                                if (!dns.empty() > 0)
                                    i.internetConnection.primaryDns = to_string(dns[0]);
                                else
                                    i.internetConnection.primaryDns = "---";

                                __DBG__
                                if (dns.size() > 1)
                                    i.internetConnection.secondaryDns = to_string(dns[1]);
                                else
                                    i.internetConnection.secondaryDns = "---";
                            }
                        }
                    } catch(...) {
                        __DBG__
                        i.internetConnection.ipAddress = "--";
                        i.internetConnection.subnetMask = "--";
                        i.internetConnection.defaultGateway = "--";
                        i.internetConnection.primaryDns = "--";
                        i.internetConnection.secondaryDns = "--";
                    }
                } else {
                    __DBG__
                    i.internetConnection.ipAddress = "-";
                    i.internetConnection.subnetMask = "-";
                    i.internetConnection.defaultGateway = "-";
                    i.internetConnection.primaryDns = "-";
                    i.internetConnection.secondaryDns = "-";
                }
            }

            Poco::JSON::Object  Answer;
            SI.to_json(Answer);
            return ReturnObject(Answer);
        }

        //  if the user does not have a device, we cannot continue.
        ProvObjects::InventoryTagList DeviceIds;
        if(!SDK::Prov::Subscriber::GetDevices(this,UserInfo_.userinfo.id,DeviceIds)) {
            return BadRequest("Provisioning service not available yet.");
        }

        if(DeviceIds.taglist.empty() ) {
            return BadRequest("No devices activated yet.");
        }

        std::cout << "Creating default subscriber info: " << UserInfo_.userinfo.id << std::endl;
        StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI, DeviceIds);
        std::cout << "Creating default subscriber info: " << SI.id << std::endl;
        StorageService()->SubInfoDB().CreateRecord(SI);
        std::cout << "Creating default subscriber info: " << SI.id << std::endl;

        std::cout << "Creating default config..." << std::endl;
        ConfigMaker     InitialConfig(SI.id);
        InitialConfig.Prepare();

        StorageService()->SubInfoDB().GetRecord("id", SI.id, SI);

        Poco::JSON::Object  Answer;
        SI.to_json(Answer);
        ReturnObject(Answer);
    }

    void RESTAPI_subscriber_handler::DoPut() {

        auto ConfigChanged = GetParameter("configChanged","true") == "true";
        auto ApplyConfigOnly = GetParameter("applyConfigOnly","true") == "true";

        if(UserInfo_.userinfo.id.empty()) {
            return NotFound();
        }

        SubObjects::SubscriberInfo  Existing;
        if(!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, Existing)) {
            return NotFound();
        }

        if(ApplyConfigOnly) {
            ConfigMaker     InitialConfig(UserInfo_.userinfo.id);
            if(InitialConfig.Prepare())
                return OK();
            else
                return InternalError("Configuration could not be refreshed.");
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
        if(ConfigChanged) {
            for (auto &New: Changes.accessPoints.list) {
                for (auto &Old: Existing.accessPoints.list) {
                    if (New.id == Old.id) {
                        Old.internetConnection.modified = Now;
                        Old.deviceMode.modified = Now;
                        Old.wifiNetworks.modified = Now;
                        Old.subscriberDevices.modified = Now;
                        Old = New;
                    }
                }
            }
        }

        if(StorageService()->SubInfoDB().UpdateRecord("id",UserInfo_.userinfo.id, Existing)) {
            if(ConfigChanged) {
                ConfigMaker     InitialConfig(UserInfo_.userinfo.id);
                InitialConfig.Prepare();
            }
            SubObjects::SubscriberInfo  Modified;
            StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.id,Modified);
            SubscriberCache()->UpdateSubInfo(UserInfo_.userinfo.id,Modified);
            Poco::JSON::Object  Answer;
            Modified.to_json(Answer);
            return ReturnObject(Answer);
        }

        return InternalError("Profile could not be updated. Try again.");
    }

}