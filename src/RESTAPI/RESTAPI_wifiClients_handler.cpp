//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wifiClients_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "sdks/SDK_gw.h"

// #define __DBG__ std::cout << __LINE__ << std::endl ;
#define __DBG__
namespace OpenWifi {

    static void AddManufacturers(SubObjects::AssociationList &List) {
        std::vector<std::pair<std::string,std::string>> MacList;
        for(const auto &i:List.associations) {
            MacList.push_back(std::make_pair(i.macAddress,""));
        }

        if(SDK::GW::Device::GetOUIs(nullptr,MacList)) {
            for(const auto &i:MacList)
                for(auto &j:List.associations)
                    if(j.macAddress==i.first) {
                        std::cout << i.first <<  " :: " << i.second << std::endl;
                        j.manufacturer = i.second;
                    }
        }
    }

    void RESTAPI_wifiClients_handler::DoGet() {
        auto SerialNumber = GetParameter("serialNumber","");
        if(SerialNumber.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }

        Logger().information(fmt::format("{}: Getting list of wireless clients.",SerialNumber));

        SubObjects::SubscriberInfo  SI;
        if(!StorageService()->SubInfoDB().GetRecord("id",UserInfo_.userinfo.id,SI)) {
            return NotFound();
        }

        for(const auto &i:SI.accessPoints.list) {
            if(i.macAddress.empty())
                continue;
            if(SerialNumber==i.macAddress) {
                Poco::JSON::Object::Ptr LastStats;
                Poco::JSON::Object  Answer;
                if(SDK::GW::Device::GetLastStats(nullptr,i.serialNumber,LastStats)){
                    uint64_t    Now = OpenWifi::Now();
                    SubObjects::AssociationList AssocList;
                    AssocList.modified = AssocList.created = Now;
                    std::stringstream SS;
                    LastStats->stringify(SS);
                    try {
                        auto stats = nlohmann::json::parse(SS.str());
                        if(stats.contains("interfaces") && stats["interfaces"].is_array()) {
                            auto ifs = stats["interfaces"];
                            for (const auto &cur_interface: ifs) {
                                //  create a map of MAC -> IP for clients
                                std::map<std::string,std::pair<std::string,std::string>>   IPs;
                                if (cur_interface.contains("clients") && cur_interface["clients"].is_array()) {
                                    auto clients = cur_interface["clients"];
                                    for(const auto &cur_client:clients) {
                                        if(cur_client.contains("mac")) {
                                            std::string ipv4,ipv6;
                                            if( cur_client.contains("ipv6_addresses") &&
                                                cur_client["ipv6_addresses"].is_array() &&
                                                !cur_client["ipv6_addresses"].empty()) {
                                                ipv6 = cur_client["ipv6_addresses"][0].get<std::string>();
                                            }
                                            if( cur_client.contains("ipv4_addresses") &&
                                                cur_client["ipv4_addresses"].is_array() &&
                                                !cur_client["ipv4_addresses"].empty()) {
                                                ipv4 = cur_client["ipv4_addresses"][0].get<std::string>();
                                            }
                                            IPs[cur_client["mac"].get<std::string>()] = std::make_pair(ipv4,ipv6);
                                        }
                                    }
                                }

                                if (cur_interface.contains("ssids") && cur_interface["ssids"].is_array() && !cur_interface["ssids"].empty()) {
                                    for (const auto &cur_ssid: cur_interface["ssids"]) {
                                        if (cur_ssid.contains("associations") && cur_ssid["associations"].is_array() && !cur_ssid["associations"].empty()) {
                                            for (const auto &cur_client: cur_ssid["associations"]) {
                                                SubObjects::Association Assoc;
                                                Assoc.ssid = cur_ssid["ssid"].get<std::string>();
                                                Assoc.macAddress = cur_client["station"].get<std::string>();
                                                Assoc.rssi = cur_client["rssi"].get<int32_t>();
                                                Assoc.rx = cur_client["rx_bytes"].get<uint64_t>();
                                                Assoc.tx = cur_client["tx_bytes"].get<uint64_t>();
                                                Assoc.power = 0;
                                                Assoc.name = cur_client["station"].get<std::string>();
                                                auto which_ips = IPs.find(Assoc.macAddress);
                                                if(which_ips != IPs.end()) {
                                                    Assoc.ipv4 = which_ips->second.first;
                                                    Assoc.ipv6 = which_ips->second.second;
                                                }
                                                AssocList.associations.push_back(Assoc);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        AddManufacturers(AssocList);
                        AssocList.to_json(Answer);
                    } catch (...) {
                    }
                }
                return ReturnObject(Answer);
            }
        }
        return NotFound();
    }

}