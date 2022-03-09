//
// Created by stephane bourque on 2021-11-07.
//

#include "RESTAPI_subscriber_handler.h"
#include "StorageService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "SubscriberCache.h"
#include "sdks/SDK_prov.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_fms.h"
#include "sdks/SDK_sec.h"

#include "ConfigMaker.h"

// #define __DBG__ std::cout << __LINE__ << std::endl ;
// #define __DBG__
namespace OpenWifi {

    template <typename T> void AssignIfModified(T & Var, const T & Value, int &Mods) {
        if(Var!=Value) {
            Var = Value;
            Mods++;
        }
    }

    void RESTAPI_subscriber_handler::DoGet() {

        if(UserInfo_.userinfo.id.empty()) {
            return NotFound();
        }

        Logger().information(Poco::format("%s: Get basic info.", UserInfo_.userinfo.email));
        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id,SI)) {

            int Mods=0;

            //  we need to get the stats for each AP
            for(auto &i:SI.accessPoints.list) {
                if(i.macAddress.empty())
                    continue;
                Poco::JSON::Object::Ptr LastStats;
                if(SDK::GW::Device::GetLastStats(nullptr,i.serialNumber,LastStats)) {
                    std::ostringstream OS;
                    LastStats->stringify(OS);
                    try {
                        nlohmann::json LA = nlohmann::json::parse(OS.str());
                        for (const auto &j: LA["interfaces"]) {
                            if (j.contains("location") && j["location"].get<std::string>()=="/interfaces/0" && j.contains("ipv4")) {

                                if( j["ipv4"]["addresses"].is_array()
                                    && !j["ipv4"]["addresses"].empty() ) {
                                    auto IPParts = Poco::StringTokenizer(j["ipv4"]["addresses"][0].get<std::string>(), "/");
                                    AssignIfModified(i.internetConnection.ipAddress,IPParts[0],Mods);
                                    i.internetConnection.subnetMask = IPParts[1];
                                }
                                if (j["ipv4"].contains("dhcp_server"))
                                    AssignIfModified(i.internetConnection.defaultGateway,j["ipv4"]["dhcp_server"].get<std::string>(),Mods);
                                else
                                    AssignIfModified(i.internetConnection.defaultGateway,std::string{"---"},Mods);

                                if (j.contains("dns_servers") && j["dns_servers"].is_array()) {
                                    auto dns = j["dns_servers"];
                                    if (!dns.empty())
                                        AssignIfModified(i.internetConnection.primaryDns,dns[0].get<std::string>(),Mods);
                                    else
                                        AssignIfModified(i.internetConnection.primaryDns,std::string{"---"},Mods);
                                    if (dns.size() > 1)
                                        AssignIfModified(i.internetConnection.secondaryDns, dns[1].get<std::string>(),Mods);
                                    else
                                        AssignIfModified(i.internetConnection.secondaryDns, std::string{"---"},Mods);
                                }
                            }
                        }
                    } catch(...) {
                        AssignIfModified(i.internetConnection.ipAddress, std::string{"--"}, Mods);
                        i.internetConnection.subnetMask = "--";
                        i.internetConnection.defaultGateway = "--";
                        i.internetConnection.primaryDns = "--";
                        i.internetConnection.secondaryDns = "--";
                    }
                } else {
                    AssignIfModified(i.internetConnection.ipAddress, std::string{"-"}, Mods);
                    i.internetConnection.subnetMask = "-";
                    i.internetConnection.defaultGateway = "-";
                    i.internetConnection.primaryDns = "-";
                    i.internetConnection.secondaryDns = "-";
                }

                FMSObjects::DeviceInformation   DI;
                if(SDK::FMS::Firmware::GetDeviceInformation(nullptr,i.serialNumber,DI)) {
                    AssignIfModified(i.currentFirmwareDate, DI.currentFirmwareDate, Mods);
                    AssignIfModified(i.currentFirmware, DI.currentFirmware, Mods);
                    AssignIfModified(i.latestFirmwareDate, DI.latestFirmwareDate, Mods);
                    AssignIfModified(i.latestFirmware, DI.latestFirmware, Mods);
                    AssignIfModified(i.newFirmwareAvailable, DI.latestFirmwareAvailable, Mods);
                    AssignIfModified(i.latestFirmwareURI, DI.latestFirmwareURI, Mods);
                }
            }

            if(Mods) {
                SI.modified = OpenWifi::Now();
                StorageService()->SubInfoDB().UpdateRecord("id", UserInfo_.userinfo.id,SI);
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

        Logger().information(Poco::format("%s: Creating default user information.", UserInfo_.userinfo.email));
        StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI, DeviceIds);
        Logger().information(Poco::format("%s: Creating default configuration.", UserInfo_.userinfo.email));
        StorageService()->SubInfoDB().CreateRecord(SI);

        Logger().information(Poco::format("%s: Generating initial configuration.", UserInfo_.userinfo.email));
        ConfigMaker     InitialConfig(Logger(),SI.id);
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
            ConfigMaker     InitialConfig(Logger(),UserInfo_.userinfo.id);
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

        auto Now = OpenWifi::Now();
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
        for (auto &NewAP: Changes.accessPoints.list) {
            for (auto &ExistingAP: Existing.accessPoints.list) {
                if (NewAP.macAddress == ExistingAP.macAddress) {
                    ExistingAP = NewAP;
                    ExistingAP.internetConnection.modified = Now;
                    ExistingAP.deviceMode.modified = Now;
                    ExistingAP.wifiNetworks.modified = Now;
                    ExistingAP.subscriberDevices.modified = Now;
                }
            }
        }

        if(StorageService()->SubInfoDB().UpdateRecord("id",UserInfo_.userinfo.id, Existing)) {
            if(ConfigChanged) {
                ConfigMaker     InitialConfig(Logger(),UserInfo_.userinfo.id);
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

    void RESTAPI_subscriber_handler::DoDelete() {
        auto id = GetParameter("id","");
        if(!id.empty()) {
            StorageService()->SubInfoDB().DeleteRecord("id",id);
            return OK();
        }

        SubObjects::SubscriberInfo  SI;
        if(StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.id,SI)) {
            for(const auto &i:SI.accessPoints.list) {
                if(!i.serialNumber.empty()) {
                    SDK::Prov::Subscriber::ReturnDeviceToInventory(nullptr, UserInfo_.userinfo.id, i.serialNumber);
                }
            }
            SDK::Sec::Subscriber::Delete(nullptr, UserInfo_.userinfo.id);
            StorageService()->SubInfoDB().DeleteRecord("id", UserInfo_.userinfo.id);
            return OK();
        }
        return NotFound();
    }
}