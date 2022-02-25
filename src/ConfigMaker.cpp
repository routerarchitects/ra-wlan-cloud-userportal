//
// Created by stephane bourque on 2021-12-13.
//

#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {

    bool ConfigMaker::Push() {
        return false;
    }

    bool ConfigMaker::Prepare() {
        SubObjects::SubscriberInfo  SI;

        std::cout << "Prepare " << __LINE__ << std::endl;
        if(!StorageService()->SubInfoDB().GetRecord("id", id_, SI)) {
            std::cout << "Prepare " << __LINE__ << std::endl;
            bad_ = true;
            return false;
        }
        std::cout << "Prepare " << __LINE__ << std::endl;

        //  We need to create the basic sections
        auto  metrics = R"(
        {
            "dhcp-snooping": {
                "filters": [ "ack", "discover", "offer", "request", "solicit", "reply", "renew" ]
            },
            "health": {
                "interval": 120
            },
            "statistics": {
                "interval": 120,
                "types": [ "ssids", "lldp", "clients" ]
            },
            "wifi-frames": {
                "filters": ["probe", "auth", "assoc", "disassoc", "deauth", "local-deauth",
                            "inactive-deauth", "key-mismatch", "beacon-report", "radar-detected" ]
            }
        } )"_json;

        auto services = R"(
        {
            "services": {
            "lldp": {
                "describe": "uCentral",
                "location": "universe"
            },
            "ssh": {
                "authorized-keys": [],
                "password-authentication": false,
                "port": 22
            }
        } )"_json;

        std::cout << "Generating configs" << std::endl;
        for(auto &i:SI.accessPoints.list) {

            nlohmann::json Interfaces;
            nlohmann::json UpstreamInterface;
            nlohmann::json DownstreamInterface;
            nlohmann::json radios;

            std::cout << "Generating configs: " << i.macAddress << std::endl;
            if(i.macAddress.empty())
                continue;

            UpstreamInterface["name"] = "WAN";
            UpstreamInterface["role"] = "upstream";
            UpstreamInterface["services"].push_back("lldp");

            std::vector<std::string>    AllBands;
            for(const auto &rr:i.radios)
                AllBands.emplace_back(rr.band);

            if(i.internetConnection.type=="manual") {
                UpstreamInterface["addressing"] = "static";
                UpstreamInterface["subnet"] = i.internetConnection.subnetMask;
                UpstreamInterface["gateway"] = i.internetConnection.defaultGateway;
                UpstreamInterface["send-hostname"] = i.internetConnection.sendHostname;
                UpstreamInterface["use-dns"].push_back(i.internetConnection.primaryDns);
                if(!i.internetConnection.secondaryDns.empty())
                    UpstreamInterface["use-dns"].push_back(i.internetConnection.secondaryDns);
            } else if(i.internetConnection.type=="pppoe") {
                nlohmann::json Port;
                Port["select-ports"].push_back("WAN*");
                UpstreamInterface["ethernet"].push_back(Port);
                UpstreamInterface["broad-band"]["protocol"] = "pppoe";
                UpstreamInterface["broad-band"]["user-name"] = i.internetConnection.username;
                UpstreamInterface["broad-band"]["password"] = i.internetConnection.password;
                UpstreamInterface["ipv4"]["addressing"] = "dynamic";
                if(i.internetConnection.ipV6Support)
                    UpstreamInterface["ipv6"]["addressing"] = "dynamic";
            } else if(i.internetConnection.type=="automatic") {
                nlohmann::json Port;
                Port["select-ports"].push_back("WAN*");
                if(i.deviceMode.type=="bridge")
                    Port["select-ports"].push_back("LAN*");
                UpstreamInterface["ethernet"].push_back(Port);
                UpstreamInterface["ipv4"]["addressing"] = "dynamic";
                if(i.internetConnection.ipV6Support)
                    UpstreamInterface["ipv6"]["addressing"] = "dynamic";
            }

            if(i.deviceMode.type=="bridge") {
                nlohmann::json ssids;
                for(auto &j:i.wifiNetworks.wifiNetworks) {
                    nlohmann::json ssid;
                    ssid["name"] = j.name ;
                    if(j.bands[0]=="all") {
                        ssid["wifi-bands"] = AllBands;
                    } else {
                        ssid["wifi-bands"] = j.bands;
                    }
                    ssid["bss-mode"] = "ap";
                    if(j.encryption.empty()) {
                        j.encryption = "wpa2-personal";
                    }

                    if(j.encryption=="wpa1-personal") {
                        ssid["encryption"]["proto"] = "psk";
                        ssid["encryption"]["ieee80211w"] = "optional";
                    } else if(j.encryption=="wpa2-personal") {
                        ssid["encryption"]["proto"] = "psk2";
                        ssid["encryption"]["ieee80211w"] = "optional";
                    } else if(j.encryption=="wpa3-personal") {
                        ssid["encryption"]["proto"] = "sae";
                        ssid["encryption"]["ieee80211w"] = "required";
                    } else if (j.encryption=="wpa1/2-personal") {
                        ssid["encryption"]["proto"] = "psk-mixed";
                        ssid["encryption"]["ieee80211w"] = "optional";
                    } else if (j.encryption=="wpa2/3-personal") {
                        ssid["encryption"]["proto"] = "sae-mixed";
                        ssid["encryption"]["ieee80211w"] = "optional";
                    }
                    ssid["encryption"]["key"] = j.password;
                    ssids.push_back(ssid);
                }
                UpstreamInterface["ssids"] = ssids;
            } else if(i.deviceMode.type=="manual") {
                DownstreamInterface["name"] = "LAN";
                DownstreamInterface["role"] = "downstream";
                DownstreamInterface["services"].push_back("lldp");
                DownstreamInterface["services"].push_back("ssh");
                nlohmann::json Port;
                Port["select-ports"].push_back("LAN*");
                DownstreamInterface["ethernet"].push_back(Port);
                DownstreamInterface["ipv4"]["addressing"] = "static";
                DownstreamInterface["ipv4"]["subnet"] = i.internetConnection.subnetMask.empty() ? "auto/24" : i.deviceMode.subnet;
                DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = 10;
                DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = 100;
                DownstreamInterface["ipv4"]["dhcp"]["lease-time"] = "6h";
            } else if(i.deviceMode.type=="nat") {
                DownstreamInterface["name"] = "LAN";
                DownstreamInterface["role"] = "downstream";
                DownstreamInterface["services"].push_back("lldp");
                DownstreamInterface["services"].push_back("ssh");
                nlohmann::json Port;
                Port["select-ports"].push_back("LAN*");
                DownstreamInterface["ethernet"].push_back(Port);
                DownstreamInterface["ipv4"]["addressing"] = "static";
                DownstreamInterface["ipv4"]["subnet"] = "192.168.1.1/24";
                DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = 10;
                DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = 100;
                DownstreamInterface["ipv4"]["dhcp"]["lease-time"] = "6h";

                nlohmann::json ssids;
                for(const auto &j:i.wifiNetworks.wifiNetworks) {
                    nlohmann::json ssid;
                    ssid["name"] = j.name ;
                    ssid["role"] = "downstream";
                    if(j.bands[0]=="all") {
                        ssid["wifi-bands"] = AllBands;
                    } else {
                        ssid["wifi-bands"] = j.bands;
                    }
                    ssid["bss-mode"] = "ap";
                    ssid["encryption"]["proto"] = j.encryption;
                    ssid["encryption"]["key"] = j.password;
                    ssid["encryption"]["ieee80211w"] = "optional";
                    ssids.push_back(ssid);
                }
                DownstreamInterface["ssids"] = ssids;
                Interfaces.push_back(DownstreamInterface);
            }
            for(const auto &k:i.radios) {
                nlohmann::json radio;

                radio["band"] = k.band;
                radio["bandwidth"] = k.bandwidth;

                if(k.channel==0)
                    radio["channel"] = "auto";
                else
                    radio["channel"] = k.channel;
                if(k.country.size()==2)
                    radio["country"] = k.country;

                radio["channel-mode"] = k.channelMode;
                radio["channel-width"] = k.channelWidth;
                if(!k.requireMode.empty())
                    radio["require-mode"] = k.requireMode;
                if(k.txpower>0)
                    radio["tx-power"] = k.txpower;
                if(k.allowDFS)
                    radio["allow-dfs"] = true;
                if(!k.mimo.empty())
                    radio["mimo"] = k.mimo;
                radio["legacy-rates"] = k.legacyRates;
                radio["beacon-interval"] = k.beaconInterval;
                radio["dtim-period"] = k.dtimPeriod;
                radio["maximum-clients"] = k.maximumClients;
                radio["rates"]["beacon"] = k.rates.beacon;
                radio["rates"]["multicast"] = k.rates.multicast;
                radio["he-settings"]["multiple-bssid"] = k.he.multipleBSSID;
                radio["he-settings"]["ema"] = k.he.ema;
                radio["he-settings"]["bss-color"] = k.he.bssColor;
                radios.push_back(radio);
            }

            ProvObjects::DeviceConfigurationElementVec Configuration;
            ProvObjects::DeviceConfigurationElement Metrics{
                    .name = "metrics",
                    .description = "default metrics",
                    .weight = 0,
                    .configuration = to_string(metrics)
            };

            ProvObjects::DeviceConfigurationElement Services{
                    .name = "services",
                    .description = "default services",
                    .weight = 0,
                    .configuration = to_string(services)
            };

            Interfaces.push_back(UpstreamInterface);
            Interfaces.push_back(DownstreamInterface);
            ProvObjects::DeviceConfigurationElement InterfacesList{
                    .name = "interfaces",
                    .description = "default interfaces",
                    .weight = 0,
                    .configuration = to_string(Interfaces)
            };
            ProvObjects::DeviceConfigurationElement RadiosList{
                    .name = "radios",
                    .description = "default radios",
                    .weight = 0,
                    .configuration = to_string(radios)
            };

            Configuration.push_back(Metrics);
            Configuration.push_back(Services);
            Configuration.push_back(InterfacesList);
            Configuration.push_back(RadiosList);

            Poco::JSON::Object  Answer;

            ProvObjects::DeviceConfiguration    Cfg;

            Cfg.deviceTypes.push_back("*");

            Cfg.firmwareRCOnly = true;
            Cfg.firmwareUpgrade = i.automaticUpgrade ? "yes" : "no";
            Cfg.configuration = Configuration;

            Cfg.to_json(Answer);

            if(i.configurationUUID.empty()) {
                //  we need to create this configuration and associate it to this device.
                Cfg.info.name = "sub:" + i.macAddress;
                Cfg.info.notes.emplace_back(SecurityObjects::NoteInfo{.created=OpenWifi::Now(), .note="Auto-created from subscriber service."});
                std::string CfgUUID;
                if(SDK::Prov::Configuration::Create(nullptr, i.macAddress, Cfg, CfgUUID)) {
                    i.configurationUUID = CfgUUID;
                    std::cout << "Created and assigned configuration: " << i.macAddress << std::endl;
                } else {
                    std::cout << "Failure to create configuration: " << i.macAddress << std::endl;
                    return false;
                }
            } else {
                ProvObjects::DeviceConfiguration    ExistingConfig;
                if(SDK::Prov::Configuration::Get(nullptr,i.configurationUUID,ExistingConfig)) {
                    Cfg.info = ExistingConfig.info;
                }

                if(SDK::Prov::Configuration::Update(nullptr,i.configurationUUID,Cfg)) {
                    std::cout << "Modified configuration: " << i.macAddress << std::endl;
                } else {
                    std::cout << "failure to modify configuration: " << i.macAddress << std::endl;
                    return false;
                }
            }
        }
        SI.modified = OpenWifi::Now();
        return StorageService()->SubInfoDB().UpdateRecord("id",id_,SI);
    }

}