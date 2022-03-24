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
                    uint64_t    Now = std::time(nullptr);
                    SubObjects::AssociationList AssocList;
                    AssocList.modified = AssocList.created = Now;
                    std::stringstream SS;
                    LastStats->stringify(SS);
                    try {
                        auto stats = nlohmann::json::parse(SS.str());
                        __DBG__
                        if(stats.contains("interfaces") && stats["interfaces"].is_array()) {
                            __DBG__
                            auto ifs = stats["interfaces"];
                            for (const auto &cur_interface: ifs) {
                                __DBG__
                                //  create a map of MAC -> IP for clients
                                std::map<std::string,std::pair<std::string,std::string>>   IPs;
                                __DBG__
                                if (cur_interface.contains("clients") && cur_interface["clients"].is_array()) {
                                    __DBG__
                                    auto clients = cur_interface["clients"];
                                    for(const auto &cur_client:clients) {
                                        __DBG__
                                        if(cur_client.contains("mac")) {
                                            __DBG__
                                            std::string ipv4,ipv6;
                                            if( cur_client.contains("ipv6_addresses") &&
                                                cur_client["ipv6_addresses"].is_array() &&
                                                !cur_client["ipv6_addresses"].empty()) {
                                                __DBG__
                                                ipv6 = cur_client["ipv6_addresses"][0].get<std::string>();
                                            }
                                            if( cur_client.contains("ipv4_addresses") &&
                                                cur_client["ipv4_addresses"].is_array() &&
                                                !cur_client["ipv4_addresses"].empty()) {
                                                __DBG__
                                                ipv4 = cur_client["ipv4_addresses"][0].get<std::string>();
                                            }
                                            __DBG__
                                            IPs[cur_client["mac"].get<std::string>()] = std::make_pair(ipv4,ipv6);
                                            __DBG__
                                        }
                                    }
                                }

                                __DBG__
                                if (cur_interface.contains("ssids") && cur_interface["ssids"].is_array() && !cur_interface["ssids"].empty()) {
                                    __DBG__
                                    for (const auto &cur_ssid: cur_interface["ssids"]) {
                                        __DBG__
                                        if (cur_ssid.contains("associations") && cur_ssid["associations"].is_array() && !cur_ssid["associations"].empty()) {
                                            __DBG__
                                            for (const auto &cur_client: cur_ssid["associations"]) {
                                                SubObjects::Association Assoc;
                                                __DBG__
                                                Assoc.ssid = cur_ssid["ssid"].get<std::string>();
                                                Assoc.macAddress = cur_client["station"].get<std::string>();
                                                Assoc.rssi = cur_client["rssi"].get<int32_t>();
                                                Assoc.rx = cur_client["rx_bytes"].get<uint64_t>();
                                                Assoc.tx = cur_client["tx_bytes"].get<uint64_t>();
                                                Assoc.power = 0;
                                                Assoc.name = cur_client["station"].get<std::string>();
                                                __DBG__
                                                auto which_ips = IPs.find(Assoc.macAddress);
                                                __DBG__
                                                if(which_ips != IPs.end()) {
                                                    Assoc.ipv4 = which_ips->second.first;
                                                    Assoc.ipv6 = which_ips->second.second;
                                                }
                                                __DBG__
                                                AssocList.associations.push_back(Assoc);
                                            }
                                        }
                                    }
                                }
                            }
                        }
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