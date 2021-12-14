//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wiredClients_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "nlohmann/json.hpp"
#include "StorageService.h"

namespace OpenWifi {

    void RESTAPI_wiredClients_handler::DoGet() {
        auto SerialNumber = GetParameter("serialNumber", "");
        if (SerialNumber.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }

        SubObjects::SubscriberInfo SI;
        if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.Id, SI)) {
            return NotFound();
        }

        for (const auto &i: SI.accessPoints.list) {
            if (SerialNumber == i.macAddress) {
                //  Get the last stats for this device
                //  https://${OWGW}/api/v1/device/$1/statistics?lastOnly=true
                Types::StringPairVec QD{{"lastOnly", "true"}};
                std::string EndPoint{"/api/v1/device/" + SerialNumber + "/statistics"};
                OpenAPIRequestGet Api(uSERVICE_GATEWAY, EndPoint, QD, 20000);

                Poco::JSON::Object::Ptr CallResponse;
                auto ResponseStatus = Api.Do(CallResponse, UserInfo_.webtoken.access_token_);
                Poco::JSON::Object Answer;
                uint64_t Now = std::time(nullptr);
                Answer.set("created", Now);
                Answer.set("modified", Now);
                SubObjects::ClientList CList;

                if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                    std::stringstream SS;
                    Poco::JSON::Stringifier::condense(CallResponse, SS);
                    try {
                        auto stats = nlohmann::json::parse(SS.str());
                        auto ifs = stats["interfaces"];

                        for (const auto &i: ifs) {
                            if (i.contains("clients")) {
                                auto clients = i["clients"];
                                for (const auto &i: clients) {
                                    SubObjects::Client C;

                                    C.macAddress = i["mac"];
                                    if (i.contains("ipv6_addresses")) {
                                        auto ipv6addresses = i["ipv6_addresses"];
                                        for (const auto &j: ipv6addresses) {
                                            C.ipv6 = j;
                                            break;
                                        }
                                    }
                                    if (i.contains("ipv4_addresses")) {
                                        auto ipv4addresses = i["ipv4_addresses"];
                                        for (const auto &j: ipv4addresses) {
                                            C.ipv4 = j;
                                        }
                                    }
                                    C.tx = C.rx = 0;
                                    C.speed = "auto";
                                    C.mode = "auto";
                                    CList.clients.push_back(C);
                                }
                            }
                        }
                        Poco::JSON::Object  P;
                        CList.to_json(P);
                        Answer.set("clients",P);
                    } catch (...) {
                    }
                }
                return ReturnObject(Answer);
            }
        }
        return NotFound();
    }
}