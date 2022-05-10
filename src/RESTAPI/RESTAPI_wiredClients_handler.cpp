//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wiredClients_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "nlohmann/json.hpp"
#include "StorageService.h"
#include "sdks/SDK_gw.h"

namespace OpenWifi {

    static void AddManufacturers(SubObjects::ClientList &List) {
        std::vector<std::pair<std::string,std::string>> MacList;
        for(const auto &i:List.clients) {
            MacList.push_back(std::make_pair(i.macAddress,""));
        }

        if(SDK::GW::Device::GetOUIs(nullptr,MacList)) {
            for(const auto &i:MacList)
                for(auto &j:List.clients)
                    if(j.macAddress==i.first) {
                        std::cout << i.first <<  " :: " << i.second << std::endl;
                        j.manufacturer = i.second;
                    }
        }
    }

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
                                std::set<std::string>   WifiMacs;
                                if (cur_interface.contains("ssids") && cur_interface["ssids"].is_array() && !cur_interface["ssids"].empty()) {
                                    for (const auto &cur_ssid: cur_interface["ssids"]) {
                                        if (cur_ssid.contains("associations") && cur_ssid["associations"].is_array() && !cur_ssid["associations"].empty()) {
                                            for (const auto &cur_client: cur_ssid["associations"]) {
                                                WifiMacs.insert(cur_client["station"].get<std::string>());
                                            }
                                        }
                                    }
                                }

                                if(cur_interface.contains("clients") && cur_interface["clients"].is_array() && !cur_interface["clients"].empty()) {
                                    auto clients = cur_interface["clients"];
                                    for (const auto &cur_client: clients) {

                                        std::string Mac = to_string(cur_client["mac"]);

                                        if(WifiMacs.find(Mac)==WifiMacs.end()) {

                                            SubObjects::Client C;

                                            C.macAddress = Mac;
                                            if (cur_client.contains("ipv6_addresses") &&
                                                cur_client["ipv6_addresses"].is_array() &&
                                                !cur_client["ipv6_addresses"].empty()) {
                                                auto ipv6addresses = cur_client["ipv6_addresses"];
                                                for (const auto &cur_addr: ipv6addresses) {
                                                    C.ipv6 = cur_addr;
                                                    break;
                                                }
                                            }
                                            if (cur_client.contains("ipv4_addresses") &&
                                                cur_client["ipv4_addresses"].is_array() &&
                                                !cur_client["ipv4_addresses"].empty()) {
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
                        }
                        AddManufacturers(CList);
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