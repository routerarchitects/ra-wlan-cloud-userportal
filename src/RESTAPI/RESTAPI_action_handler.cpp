//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "SubscriberCache.h"
#include "StorageService.h"
#include "sdks/SDK_gw.h"

namespace OpenWifi {

    void RESTAPI_action_handler::DoPost() {
        auto Command = GetParameter("action","");

        if(Command.empty()) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        auto Body = ParseStream();
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
            StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI);
            StorageService()->SubInfoDB().CreateRecord(SI);
            if(!SubscriberCache()->GetSubInfo(UserInfo_.userinfo.id,SubInfo))
                return NotFound();
        }

        for(const auto &i:SubInfo->accessPoints.list) {
            if(i.macAddress == Mac) {
                if(Command == "reboot") {
                    return SDK::GW::Device::Reboot(this, Mac, When);
                } else if(Command == "blink") {
                    return SDK::GW::Device::LEDs(this, Mac, When, Duration, Pattern);
                } else if(Command == "upgrade") {
                    return SDK::GW::Device::Upgrade(this, Mac, When, ImageName, keepRedirector);
                } else if(Command == "factory") {
                    return SDK::GW::Device::Factory(this, Mac, When, keepRedirector);
                } else if(Command == "refresh") {
                    return SDK::GW::Device::Refresh(this, Mac, When);
                } else {
                    return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
                }
            }
        }
        return NotFound();
    }
}