//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wifiClients_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"

namespace OpenWifi {

    void RESTAPI_wifiClients_handler::DoGet() {
        auto SerialNumber = GetParameter("serialNumber","");
        if(SerialNumber.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }

        SubObjects::SubscriberInfo  SI;
        if(!StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.Id,SI)) {
            return NotFound();
        }

        for(const auto &i:SI.accessPoints.list) {
            if(SerialNumber==i.macAddress) {
                //  Get the last stats for this device
                //  https://${OWGW}/api/v1/device/$1/statistics?lastOnly=true
                Types::StringPairVec    QD{ { "lastOnly" , "true"}};
                std::string             EndPoint{ "/api/v1/device/" + SerialNumber + "/statistics"};
                OpenAPIRequestGet       Api(uSERVICE_GATEWAY, EndPoint, QD, 20000);

                Poco::JSON::Object::Ptr CallResponse;
                auto ResponseStatus = Api.Do(CallResponse, UserInfo_.webtoken.access_token_);
                Poco::JSON::Object  Answer;
                uint64_t    Now = std::time(nullptr);
                Answer.set("created",Now);
                Answer.set("modified", Now);
                SubObjects::AssociationList AssocList;

                if( ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                    std::stringstream SS;
                    Poco::JSON::Stringifier::condense(CallResponse,SS);
                    try {
                        auto stats = nlohmann::json::parse(SS.str());
                        auto ifs = stats["interfaces"];

                        for(const auto &i:ifs) {
                            if(i.contains("ssids")) {
                                for(const auto &j:i["ssids"]) {
                                    if(j.contains("associations")) {
                                        for(const auto &k:j["associations"]) {
                                            SubObjects::Association Assoc;

                                            Assoc.ssid = j["ssid"];
                                            Assoc.macAddress = k["station"];
                                            Assoc.rssi = k["rssi"];
                                            Assoc.rx = k["rx_bytes"];
                                            Assoc.tx = k["tx_bytes"];
                                            Assoc.power = 0 ;
                                            Assoc.name = "" ;
                                            AssocList.associations.push_back(Assoc);
                                        }
                                    }
                                }
                            }
                        }
                        Poco::JSON::Object  P;
                        AssocList.to_json(P);
                        Answer.set("associations",P);
                    } catch (...) {
                    }
                }
                return ReturnObject(Answer);
            }
        }
        return NotFound();
    }

}