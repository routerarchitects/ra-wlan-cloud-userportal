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

    static std::string ConvertBand(const std::string &B) {
        if(B=="2G") return "2G";
        if(B=="6G") return "6G";
        if(B=="5GU") return "5G-upper";
        if(B=="5GL") return "5G-lower";
        return B;
    }

    static std::vector<std::string> ConvertBands(const std::vector<std::string> &Bs) {
        std::vector<std::string> R;
        for(const auto &i:Bs)
            R.emplace_back(ConvertBand(i));
        return R;
    }

    void CreateDHCPInfo( std::string &Subnet, const std::string &First, const std::string &Last, std::string & DHCPFirst, uint64_t & HowMany) {
        Poco::Net::IPAddress    SubnetAddr, FirstAddress, LastAddress;
        auto Tokens = Poco::StringTokenizer(Subnet,"/");
        if(!Poco::Net::IPAddress::tryParse(Tokens[0],SubnetAddr) || !Poco::Net::IPAddress::tryParse(First,FirstAddress) || !Poco::Net::IPAddress::tryParse(Last,LastAddress)) {
            Subnet = "192.168.1.1/24";
            DHCPFirst = "192.168.1.10";
            HowMany = 100;
            return;
        }

        if(LastAddress<FirstAddress)
            std::swap(LastAddress,FirstAddress);

        struct in_addr  FA{*static_cast<const in_addr *>(FirstAddress.addr())},
                LA{*static_cast<const in_addr *>(LastAddress.addr())};

        DHCPFirst = inet_ntoa(FA);
        HowMany = htonl(LA.s_addr) - htonl(FA.s_addr);
    }

#define __DBG__ std::cout << __LINE__ << std::cout ;
    bool ConfigMaker::Prepare() {

        SubObjects::SubscriberInfo  SI;
        if(!StorageService()->SubInfoDB().GetRecord("id", id_, SI)) {
            bad_ = true;
            return false;
        }

        //  We need to create the basic sections
        auto  metrics = R"(
            {
              "metrics": {
                "dhcp-snooping": {
                  "filters": [
                    "ack",
                    "discover",
                    "offer",
                    "request",
                    "solicit",
                    "reply",
                    "renew"
                  ]
                },
                "health": {
                  "interval": 60
                },
                "statistics": {
                  "interval": 60,
                  "types": [
                    "ssids",
                    "lldp",
                    "clients"
                  ]
                },
                "wifi-frames": {
                  "filters": [
                    "probe",
                    "auth",
                    "assoc",
                    "disassoc",
                    "deauth",
                    "local-deauth",
                    "inactive-deauth",
                    "key-mismatch",
                    "beacon-report",
                    "radar-detected"
                  ]
                }
              }
            }
         )"_json;

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
            }
        } )"_json;

        bool configModified = false;

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
                AllBands.emplace_back(ConvertBand(rr.band));

            nlohmann::json UpstreamPort, DownstreamPort;
            __DBG__
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
            __DBG__

            if(i.deviceMode.type=="bridge") {
                UpstreamPort["select-ports"].push_back("LAN*");
                UpstreamPort["select-ports"].push_back("WAN*");
            } else if(i.deviceMode.type=="manual") {
                UpstreamPort.push_back("WAN*");
                DownstreamPort.push_back("LAN*");
                DownstreamInterface["name"] = "LAN";
                DownstreamInterface["role"] = "downstream";
                DownstreamInterface["services"].push_back("lldp");
                DownstreamInterface["services"].push_back("ssh");
                DownstreamInterface["ipv4"]["addressing"] = "static";
                uint64_t HowMany=0;
                std::string FirstIPInRange;
                CreateDHCPInfo(i.internetConnection.subnetMask,i.deviceMode.startIP,i.deviceMode.endIP,FirstIPInRange,HowMany);
                DownstreamInterface["ipv4"]["subnet"] = i.internetConnection.subnetMask;
                DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = FirstIPInRange;
                DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = HowMany;
                DownstreamInterface["ipv4"]["dhcp"]["lease-time"] = i.deviceMode.leaseTime.empty() ? "24h" : i.deviceMode.leaseTime;
            } else if(i.deviceMode.type=="nat") {
                UpstreamPort.push_back("WAN*");
                DownstreamPort.push_back("LAN*");
                DownstreamInterface["name"] = "LAN";
                DownstreamInterface["role"] = "downstream";
                DownstreamInterface["services"].push_back("lldp");
                DownstreamInterface["services"].push_back("ssh");
                DownstreamInterface["ipv4"]["addressing"] = "static";
                uint64_t HowMany=0;
                std::string FirstIPInRange;
                CreateDHCPInfo(i.internetConnection.subnetMask,i.deviceMode.startIP,i.deviceMode.endIP,FirstIPInRange,HowMany);
                DownstreamInterface["ipv4"]["subnet"] = i.internetConnection.subnetMask;
                DownstreamInterface["ipv4"]["dhcp"]["lease-first"] = FirstIPInRange;
                DownstreamInterface["ipv4"]["dhcp"]["lease-count"] = HowMany;
                DownstreamInterface["ipv4"]["dhcp"]["lease-time"] = i.deviceMode.leaseTime.empty() ? "24h" : i.deviceMode.leaseTime;
            }
            __DBG__
            bool hasGuest=false;
            nlohmann::json main_ssids, guest_ssids;
            __DBG__
            for(const auto &j:i.wifiNetworks.wifiNetworks) {
                nlohmann::json ssid;
                ssid["name"] = j.name ;
                if(j.bands[0]=="all") {
                    ssid["wifi-bands"] = AllBands;
                } else {
                    ssid["wifi-bands"] = ConvertBands(j.bands);
                }
                ssid["bss-mode"] = "ap";
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
                if(j.type=="main") {
                    main_ssids.push_back(ssid);
                }
                else {
                    hasGuest = true;
                    ssid["isolate-clients"] = true;
                    guest_ssids.push_back(ssid);
                }
            }
            __DBG__

            if(i.deviceMode.type=="bridge")
                UpstreamInterface["ssids"] = main_ssids;
            else
                DownstreamInterface["ssids"] = main_ssids;
            __DBG__

            if(!UpstreamPort.empty())
                UpstreamInterface["ethernet"] = UpstreamPort;
            if(!DownstreamPort.empty())
                DownstreamInterface["ethernet"] = DownstreamPort;
            __DBG__

            if(i.deviceMode.type=="bridge") {
                Interfaces.push_back(UpstreamInterface);
            } else {
                Interfaces.push_back(UpstreamInterface);
                Interfaces.push_back(DownstreamInterface);
            }
            __DBG__

            if(hasGuest) {
                __DBG__
                nlohmann::json GuestInterface;
                GuestInterface["name"] = "Guest";
                GuestInterface["role"] = "downstream";
                GuestInterface["isolate-hosts"] = true;
                GuestInterface["ipv4"]["addressing"] = "static";
                GuestInterface["ipv4"]["subnet"] = "192.168.10.1/24";
                GuestInterface["ipv4"]["subnet"]["lease-first"] = 10;
                GuestInterface["ipv4"]["subnet"]["lease-count"] = 100;
                GuestInterface["ipv4"]["subnet"]["lease-time"] = "6h";
                GuestInterface["ssids"] = guest_ssids;
                Interfaces.push_back(GuestInterface);
                __DBG__
            }

            for(const auto &k:i.radios) {
                nlohmann::json radio;
                __DBG__

                radio["band"] = ConvertBand(k.band);
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
            __DBG__

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

            nlohmann::json InterfaceSection;
            InterfaceSection["interfaces"] = Interfaces;
            ProvObjects::DeviceConfigurationElement InterfacesList{
                    .name = "interfaces",
                    .description = "default interfaces",
                    .weight = 0,
                    .configuration = to_string(InterfaceSection)
            };

            nlohmann::json RadiosSection;
            RadiosSection["radios"] = radios;
            ProvObjects::DeviceConfigurationElement RadiosList{
                    .name = "radios",
                    .description = "default radios",
                    .weight = 0,
                    .configuration = to_string(RadiosSection)
            };

            __DBG__
            Configuration.push_back(Metrics);
            Configuration.push_back(Services);
            Configuration.push_back(InterfacesList);
            Configuration.push_back(RadiosList);
            __DBG__

            Poco::JSON::Object  Answer;

            ProvObjects::DeviceConfiguration    Cfg;

            Cfg.deviceTypes.push_back(i.deviceType);
            __DBG__

            Cfg.firmwareRCOnly = true;
            Cfg.firmwareUpgrade = i.automaticUpgrade ? "yes" : "no";
            __DBG__

            Cfg.configuration = Configuration;
            __DBG__

            Cfg.to_json(Answer);
            __DBG__

            if(i.configurationUUID.empty()) {
                //  we need to create this configuration and associate it to this device.
                __DBG__
                Cfg.subscriberOnly = true;
                Cfg.subscriber = SI.userId;
                Cfg.info.name = "sub:" + i.macAddress;
                __DBG__
                Cfg.info.notes.emplace_back(SecurityObjects::NoteInfo{.created=OpenWifi::Now(), .note="Auto-created from subscriber service."});
                std::string CfgUUID;
                __DBG__
                if(SDK::Prov::Configuration::Create(nullptr, i.serialNumber, Cfg, CfgUUID)) {
                    i.configurationUUID = CfgUUID;
                    std::cout << "Created and assigned configuration: " << i.macAddress << std::endl;
                    // now push the configuration to the device...
                    ProvObjects::InventoryConfigApplyResult Results;
                    if(SDK::Prov::Configuration::Push(nullptr, i.serialNumber, Results)) {
                        std::cout << "Configuration pushed" << std::endl;
                    } else {
                        std::cout << "Configuration was not pushed" << std::endl;
                    }
                } else {
                    std::cout << "Failure to create configuration: " << i.macAddress << std::endl;
                    return false;
                }
            } else {
                __DBG__
                ProvObjects::DeviceConfiguration    ExistingConfig;
                __DBG__
                if(SDK::Prov::Configuration::Get(nullptr,i.configurationUUID,ExistingConfig)) {
                    Cfg.info = ExistingConfig.info;
                }

                __DBG__
                if(SDK::Prov::Configuration::Update(nullptr,i.configurationUUID,Cfg)) {
                    std::cout << "Modified configuration: " << i.macAddress << std::endl;
                    // Now push the configuration...
                    ProvObjects::InventoryConfigApplyResult Results;
                    if(SDK::Prov::Configuration::Push(nullptr, i.serialNumber, Results)) {
                        std::cout << "Configuration pushed" << std::endl;
                    } else {
                        std::cout << "Configuration was not pushed" << std::endl;
                    }
                } else {
                    std::cout << "failure to modify configuration: " << i.macAddress << std::endl;
                    return false;
                }
            }
        }
        __DBG__
        SI.modified = OpenWifi::Now();
        return StorageService()->SubInfoDB().UpdateRecord("id",id_,SI);
    }

}