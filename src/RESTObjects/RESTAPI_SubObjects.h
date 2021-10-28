//
// Created by stephane bourque on 2021-10-27.
//

#ifndef OWSUB_RESTAPI_SUBOBJECTS_H
#define OWSUB_RESTAPI_SUBOBJECTS_H

#include <string>

#include "Poco/JSON/Object.h"

namespace OpenWifi {

    struct HomeDeviceMode {
        bool            enableLEDS = true;
        std::string     type;       // bridge, manual, automatic
        std::string     subnet;
        std::string     subnetMask;
        std::string     startIP;
        std::string     endIP;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct IPReservation  {
        std::string     nickname;
        std::string     ipAddress;
        std::string     macAddress;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct DnsConfiguration {
        bool            ISP;
        bool            custom;
        std::string     primary;
        std::string     secondary;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct InternetConnection {
        std::string     type; // automatic, pppoe, manual
        std::string     username;
        std::string     password;
        std::string     ipAddress;
        std::string     subNetMask;
        std::string     defaultGateway;
        std::string     primaryDns;
        std::string     secondaryDns;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct WifiNetwork {
        std::string     type;       // main, guest
        std::string     name;
        std::string     password;
        std::string     encryption;
        std::vector<std::string>    bands;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct WifiNetworkList {
        std::vector<WifiNetwork>    wifiNetworks;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct AccessTime {
        std::string day;
        std::vector<std::string>    rangeList;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct AccessTimes {
        std::vector<AccessTime> schedule;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct SubscriberDevice {
        std::string     name;
        std::string     description;
        std::string     macAddress;
        std::string     manufacturer;
        uint64_t        firstContact;
        uint64_t        lastContact;
        std::string     group;
        std::string     icon;
        bool            suspended;
        std::string     ip;
        std::vector<AccessTimes>    schedule;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct SubscriberDeviceList {
        std::vector<SubscriberDevice>   devices;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct Association {
        std::string     name;
        std::string     macAddress;
        int             rssi;
        int             power;
        std::string     ipv4;
        std::string     ipv6;
        uint64_t        tx;
        uint64_t        rx;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct AssociationList {
        std::vector<Association>    associations;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct Client {
        std::string     macAddress;
        std::string     speed;
        std::string     mode;
        std::string     ipv4;
        std::string     ipv6;
        uint64_t        tx;
        uint64_t        rx;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

    struct ClientList {
        std::vector<Client> clients;

        void to_json(Poco::JSON::Object &Obj) const;
        bool from_json(const Poco::JSON::Object::Ptr &Obj);
    };

}

#endif //OWSUB_RESTAPI_SUBOBJECTS_H
