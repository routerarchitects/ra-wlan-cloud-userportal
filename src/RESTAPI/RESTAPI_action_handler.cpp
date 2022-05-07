//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "SubscriberCache.h"
#include "StorageService.h"
#include "sdks/SDK_gw.h"
#include "ConfigMaker.h"

namespace OpenWifi {

    void RESTAPI_action_handler::DoPost() {
        auto Command = GetParameter("action","");

        if(Command.empty()) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        const auto & Body = ParsedBody_;
        std::string Mac, ImageName,Pattern{"blink"};
        AssignIfPresent(Body,"mac",Mac);

        Poco::toLowerInPlace(Mac);
        Poco::trimInPlace(Mac);
        if(Mac.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }
        uint64_t    When=0, Duration = 30;
        bool keepRedirector=true;
        AssignIfPresent(Body, "when",When);
        AssignIfPresent(Body, "duration", Duration);
        AssignIfPresent(Body, "uri", ImageName);
        AssignIfPresent(Body, "pattern", Pattern);
        AssignIfPresent(Body, "keepRedirector",keepRedirector);

        Poco::SharedPtr<SubObjects::SubscriberInfo>     SubInfo;
        auto UserFound = SubscriberCache()->GetSubInfo(UserInfo_.userinfo.id,SubInfo);
        if(!UserFound) {
            SubObjects::SubscriberInfo  SI;
            if(!StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.id,SI))
                return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
        }

        for(const auto &i:SubInfo->accessPoints.list) {
            if(i.macAddress == Mac) {
                if(Command == "reboot") {
                    return SDK::GW::Device::Reboot(this, i.serialNumber, When);
                } else if(Command == "blink") {
                    return SDK::GW::Device::LEDs(this, i.serialNumber, When, Duration, Pattern);
                } else if(Command == "upgrade") {
                    return SDK::GW::Device::Upgrade(this, i.serialNumber, When, i.latestFirmwareURI, keepRedirector);
                } else if(Command == "factory") {
                    return SDK::GW::Device::Factory(this, i.serialNumber, When, keepRedirector);
                } else if(Command == "refresh") {
                    ConfigMaker     InitialConfig(Logger(),UserInfo_.userinfo.id);
                    if(InitialConfig.Prepare())
                        return OK();
                    else
                        return InternalError(RESTAPI::Errors::SubConfigNotRefreshed);
                } else {
                    return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
                }
            }
        }
        return NotFound();
    }
}