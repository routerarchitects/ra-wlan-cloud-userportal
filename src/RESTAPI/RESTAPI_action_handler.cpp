//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "SubscriberCache.h"
#include "StorageService.h"

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
        auto UserFound = SubscriberCache()->GetSubInfo(UserInfo_.userinfo.Id,SubInfo);
        if(!UserFound) {
            SubObjects::SubscriberInfo  SI;
            StorageService()->SubInfoDB().CreateDefaultSubscriberInfo(UserInfo_, SI);
            StorageService()->SubInfoDB().CreateRecord(SI);
            if(!SubscriberCache()->GetSubInfo(UserInfo_.userinfo.Id,SubInfo))
                return NotFound();
        }

        for(const auto &i:SubInfo->accessPoints.list) {
            if(i.macAddress == Mac) {
                if(Command == "reboot") {
                    return Reboot(Mac, When);
                } else if(Command == "blink") {
                    return LEDs(Mac, When, Duration, Pattern);
                } else if(Command == "upgrade") {
                    return Upgrade(Mac, When, ImageName, keepRedirector);
                } else if(Command == "factory") {
                    return Factory(Mac, When, keepRedirector);
                } else if(Command == "refresh") {
                    return Refresh(Mac, When);
                } else {
                    return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
                }
            }
        }
        return NotFound();
    }

    void RESTAPI_action_handler::Reboot(const std::string & Mac, uint64_t When) {
        std::string         EndPoint = "/api/v1/device/" + Mac + "/reboot";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",0);

        PerformCommand("reboot",EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::LEDs(const std::string & Mac, uint64_t When, uint64_t Duration, const std::string & Pattern) {
        std::string         EndPoint = "/api/v1/device/" + Mac + "/leds";
        Poco::JSON::Object  ObjRequest;

        std::cout << "M:" << Mac << " P:" << Pattern << std::endl;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("duration",Duration);
        ObjRequest.set("pattern", Pattern);
        PerformCommand("leds",EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Factory(const std::string & Mac, uint64_t When, bool KeepRedirector) {
        std::string         EndPoint = "/api/v1/device/" + Mac + "/factory";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("keepRedirector",KeepRedirector);
        PerformCommand("factory",EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Upgrade(const std::string & Mac, uint64_t When, const std::string & ImageName, bool KeepRedirector) {
        std::string         EndPoint = "/api/v1/device/" + Mac + "/upgrade";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        ObjRequest.set("uri",ImageName);
        PerformCommand("upgrade",EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::Refresh(const std::string & Mac, uint64_t When) {
        std::string         EndPoint = "/api/v1/device/" + Mac + "/refresh";
        Poco::JSON::Object  ObjRequest;

        ObjRequest.set("serialNumber", Mac);
        ObjRequest.set("when",When);
        PerformCommand("refresh",EndPoint, ObjRequest);
    }

    void RESTAPI_action_handler::PerformCommand(const std::string &Command, const std::string & EndPoint, Poco::JSON::Object & CommandRequest) {

        auto API = OpenAPIRequestPost(uSERVICE_GATEWAY, EndPoint, Types::StringPairVec{}, CommandRequest, 20000);

        Poco::JSON::Object::Ptr CallResponse;

        auto ResponseStatus = API.Do(CallResponse, UserInfo_.webtoken.access_token_);

        if(ResponseStatus != Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT) {
            Poco::JSON::Object  ResponseObject;
            ResponseObject.set("Code",Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT);
            ResponseObject.set("Details","Command could not complete, you may want to retry this operation later.");
            ResponseObject.set("Operation",Command);
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

            Poco::JSON::Parser  P;
            auto Raw = P.parse(SS.str()).extract<Poco::JSON::Object::Ptr>();
            if(Raw->get("command") && Raw->get("errorCode") && Raw->get("errorText")) {
                Poco::JSON::Object  ReturnResponse;
                ReturnResponse.set("Operation", Raw->get("command").toString());
                ReturnResponse.set("Details", Raw->get("errorText").toString());
                ReturnResponse.set("Code", Raw->get("errorCode"));

                std::stringstream  Ret;
                Poco::JSON::Stringifier::condense(ReturnResponse,Ret);
                Response->setContentLength(Ret.str().size());
                Response->setContentType("application/json");
                auto & os = Response->send();
                os << Ret.str();
            }
        }
    }

}