//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wiredClients_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "nlohmann/json.hpp"
#include "StorageService.h"
#include "sdks/SDK_gw.h"

namespace OpenWifi {

    void RESTAPI_wiredClients_handler::DoGet() {
        auto SerialNumber = GetParameter("serialNumber", "");
        if (SerialNumber.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }

        Logger().information(fmt::format("{}: Getting list of wired clients.",SerialNumber));

        SubObjects::SubscriberInfo SI;
        if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI)) {
            return NotFound();
        }

        for (const auto &i: SI.accessPoints.list) {
            if (SerialNumber == i.macAddress) {

                Poco::JSON::Object Answer;
                uint64_t Now = OpenWifi::Now();
                Answer.set("created", Now);
                Answer.set("modified", Now);
                SubObjects::ClientList CList;
                CList.modified = CList.created = Now;
                Poco::JSON::Object::Ptr LastStats;

                if(SDK::GW::Device::GetLastStats(nullptr,i.serialNumber,LastStats)){

                    SubObjects::AssociationList AssocList;
                    AssocList.modified = AssocList.created = Now;
                    std::stringstream SS;
                    LastStats->stringify(SS);
                    try {
                        auto Stats = nlohmann::json::parse(SS.str());
                        if(Stats.contains("interfaces") && Stats["interfaces"].is_array()) {
                            auto interfaces = Stats["interfaces"];
                            for (const auto &cur_interface: interfaces) {
                                if(cur_interface.contains("clients") && cur_interface["clients"].is_array() && !cur_interface["clients"].empty()) {
                                    auto clients = cur_interface["clients"];
                                    for (const auto &cur_client: clients) {
                                        SubObjects::Client C;

                                        C.macAddress = cur_client["mac"];
                                        if (cur_client.contains("ipv6_addresses") && cur_client["ipv6_addresses"].is_array() && !cur_client["ipv6_addresses"].empty()) {
                                            auto ipv6addresses = cur_client["ipv6_addresses"];
                                            for (const auto &cur_addr: ipv6addresses) {
                                                C.ipv6 = cur_addr;
                                                break;
                                            }
                                        }
                                        if (cur_client.contains("ipv4_addresses") && cur_client["ipv4_addresses"].is_array() && !cur_client["ipv4_addresses"].empty()) {
                                            auto ipv4addresses = cur_client["ipv4_addresses"];
                                            for (const auto &cur_addr: ipv4addresses) {
                                                C.ipv4 = cur_addr;
                                            }
                                        }
                                        C.tx = C.rx = 0;
                                        C.speed = "auto";
                                        C.mode = "auto";
                                        CList.clients.push_back(C);
                                    }
                                }
                            }
                        }
                        CList.to_json(Answer);
                    } catch (...) {
                    }
                }
                return ReturnObject(Answer);
            }
        }
        return NotFound();
    }
}