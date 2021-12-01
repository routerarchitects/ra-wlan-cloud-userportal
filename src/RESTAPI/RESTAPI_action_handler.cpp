//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "SubscriberCache.h"

namespace OpenWifi {

    void RESTAPI_action_handler::DoPost() {
        auto Command = GetParameter("action","");
        if(Command.empty()) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        auto Body = ParseStream();
        std::string Mac, ImageName,Pattern{"blink"};
        AssignIfPresent(Body,"mac",Mac);
        uint64_t    When=0, Duration = 30;
        bool keepRedirector=true;
        AssignIfPresent(Body, "when",When);
        AssignIfPresent(Body, "duration", Duration);
        AssignIfPresent(Body, "uri", ImageName);
        AssignIfPresent(Body, "pattern", Pattern);
        AssignIfPresent(Body, "keepRedirector",keepRedirector);

        if(Mac.empty()) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        Poco::SharedPtr<SubObjects::SubscriberInfo>     SubInfo;
        auto UserFound = SubscriberCache()->GetSubInfo(UserInfo_.userinfo.Id,SubInfo);
        if(!UserFound) {
            return NotFound();
        }

        for(const auto &i:SubInfo->accessPoints.list) {
            if(i.macAddress == Mac) {
                if(Command == "reboot") {
                    Reboot(Mac, When);
                } else if(Command == "leds") {
                    LEDs(Mac, When, Duration, Pattern);
                } else if(Command == "upgrade") {
                    Upgrade(Mac, When, ImageName, keepRedirector);
                } else if(Command == "factory") {
                    Factory(Mac, When, keepRedirector);
                } else if(Command == "refresh") {
                    Refresh(Mac, When);
                } else {
                    return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
                }

            }
        }
        return NotFound();
    }

    void RESTAPI_action_handler::Reboot(const std::string & Mac, uint64_t When) {
        std::string         EndPoint = "/device/" + Mac + "/reboot";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",0);

        PerformCommand(EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::LEDs(const std::string & Mac, uint64_t When, uint64_t Duration, const std::string & Pattern) {
        std::string         EndPoint = "/device/" + Mac + "/leds";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("duration",Duration);
        ObjRequest.set("pattern", Pattern);
        PerformCommand(EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Factory(const std::string & Mac, uint64_t When, bool KeepRedirector) {
        std::string         EndPoint = "/device/" + Mac + "/factory";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("keepRedirector",KeepRedirector);
        PerformCommand(EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Upgrade(const std::string & Mac, uint64_t When, const std::string & ImageName, bool KeepRedirector) {
        std::string         EndPoint = "/device/" + Mac + "/upgrade";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("uri",ImageName);
        PerformCommand(EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Refresh(const std::string & Mac, uint64_t When) {
        std::string         EndPoint = "/device/" + Mac + "/refresh";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        PerformCommand(EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::PerformCommand(const std::string & EndPoint, Poco::JSON::Object & CommandRequest) {
        auto API = OpenAPIRequestPost(uSERVICE_GATEWAY, EndPoint, Types::StringPairVec{}, CommandRequest, 20000);
        Poco::JSON::Object::Ptr CallResponse;

        auto ResponseStatus = API.Do(CallResponse);

        if(ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT) {
            Poco::JSON::Object  ResponseObject;
            ResponseObject.set("ErrorCode",Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT);
            ResponseObject.set("ErrorDetails","Command could not complete in time.");
            ResponseObject.set("ErrorDescription","Command could not complete, you may want to retry this operation later.");
            Response->setStatus(ResponseStatus);
            std::stringstream SS;
            Poco::JSON::Stringifier::condense(ResponseObject,SS);
            Response->setContentLength(SS.str().size());
            Response->setContentType("application/json");
            auto & os = Response->send();
            os << SS.str();
        } else {
            Response->setStatus(ResponseStatus);
            std::stringstream SS;
            Poco::JSON::Stringifier::condense(CallResponse,SS);
            Response->setContentLength(SS.str().size());
            Response->setContentType("application/json");
            auto & os = Response->send();
            os << SS.str();
        }
    }

}