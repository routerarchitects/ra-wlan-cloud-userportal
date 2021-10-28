//
// Created by stephane bourque on 2021-10-27.
//

#include "RESTAPI_SubObjects.h"
#include "framework/MicroService.h"

using OpenWifi::RESTAPI_utils::field_to_json;
using OpenWifi::RESTAPI_utils::field_from_json;


namespace OpenWifi {

    void HomeDeviceMode::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "enableLEDS", enableLEDS);
        field_to_json(Obj, "type", type);
        field_to_json(Obj, "subnet", subnet);
        field_to_json(Obj, "subnetMask", subnetMask);
        field_to_json(Obj, "startIP", startIP);
        field_to_json(Obj, "endIP", endIP);
    }

    bool HomeDeviceMode::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "enableLEDS", enableLEDS);
            field_from_json(Obj, "type", type);
            field_from_json(Obj, "subnet", subnet);
            field_from_json(Obj, "subnetMask", subnetMask);
            field_from_json(Obj, "startIP", startIP);
            field_from_json(Obj, "endIP", endIP);
            return true;
        } catch (...) {
        }
        return false;
    }

    void IPReservation::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "nickname", nickname);
        field_to_json(Obj, "ipAddress", ipAddress);
        field_to_json(Obj, "macAddress", macAddress);
    }

    bool IPReservation::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "nickname", nickname);
            field_from_json(Obj, "ipAddress", ipAddress);
            field_from_json(Obj, "macAddress", macAddress);
            return true;
        } catch (...) {
        }
        return false;
    }

    void DnsConfiguration::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "ISP", ISP);
        field_to_json(Obj, "custom", custom);
        field_to_json(Obj, "primary", primary);
        field_to_json(Obj, "secondary", secondary);
    }

    bool DnsConfiguration::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "ISP", ISP);
            field_from_json(Obj, "custom", custom);
            field_from_json(Obj, "primary", primary);
            field_from_json(Obj, "secondary", secondary);
            return true;
        } catch (...) {
        }
        return false;
    }

    void InternetConnection::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "type", type);
        field_to_json(Obj, "username", username);
        field_to_json(Obj, "password", password);
        field_to_json(Obj, "ipAddress", ipAddress);
        field_to_json(Obj, "subNetMask", subNetMask);
        field_to_json(Obj, "defaultGateway", defaultGateway);
        field_to_json(Obj, "primaryDns", primaryDns);
        field_to_json(Obj, "secondaryDns", secondaryDns);
    }

    bool InternetConnection::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "type", type);
            field_from_json(Obj, "username", username);
            field_from_json(Obj, "password", password);
            field_from_json(Obj, "ipAddress", ipAddress);
            field_from_json(Obj, "subNetMask", subNetMask);
            field_from_json(Obj, "defaultGateway", defaultGateway);
            field_from_json(Obj, "primaryDns", primaryDns);
            field_from_json(Obj, "secondaryDns", secondaryDns);
            return true;
        } catch (...) {
        }
        return false;
    }

    void WifiNetwork::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "type", type);
        field_to_json(Obj, "name", name);
        field_to_json(Obj, "password", password);
        field_to_json(Obj, "encryption", encryption);
        field_to_json(Obj, "bands", bands);
    }

    bool WifiNetwork::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "type", type);
            field_from_json(Obj, "name", name);
            field_from_json(Obj, "password", password);
            field_from_json(Obj, "encryption", encryption);
            field_from_json(Obj, "bands", bands);
            return true;
        } catch (...) {
        }
        return false;
    }

    void WifiNetworkList::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "wifiNetworks", wifiNetworks);
    }

    bool WifiNetworkList::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "wifiNetworks", wifiNetworks);
            return true;
        } catch (...) {
        }
        return false;
    }

    void AccessTime::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "day", day);
        field_to_json(Obj, "rangeList", rangeList);
    }

    bool AccessTime::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "day", day);
            field_from_json(Obj, "rangeList", rangeList);
            return true;
        } catch (...) {
        }
        return false;
    }

    void AccessTimes::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "schedule", schedule);
    }

    bool AccessTimes::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "schedule", schedule);
            return true;
        } catch (...) {
        }
        return false;
    }

    void SubscriberDevice::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "name", name);
        field_to_json(Obj, "description", description);
        field_to_json(Obj, "macAddress", macAddress);
        field_to_json(Obj, "manufacturer", manufacturer);
        field_to_json(Obj, "firstContact", firstContact);
        field_to_json(Obj, "lastContact", lastContact);
        field_to_json(Obj, "group", group);
        field_to_json(Obj, "icon", icon);
        field_to_json(Obj, "suspended", suspended);
        field_to_json(Obj, "ip", ip);
        field_to_json(Obj, "schedule", schedule);
    }

    bool SubscriberDevice::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "name", name);
            field_from_json(Obj, "description", description);
            field_from_json(Obj, "macAddress", macAddress);
            field_from_json(Obj, "manufacturer", manufacturer);
            field_from_json(Obj, "firstContact", firstContact);
            field_from_json(Obj, "lastContact", lastContact);
            field_from_json(Obj, "group", group);
            field_from_json(Obj, "icon", icon);
            field_from_json(Obj, "suspended", suspended);
            field_from_json(Obj, "ip", ip);
            field_from_json(Obj, "schedule", schedule);
            return true;
        } catch (...) {
        }
        return false;
    }

    void SubscriberDeviceList::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "devices", devices);
    }

    bool SubscriberDeviceList::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "devices", devices);
            return true;
        } catch (...) {
        }
        return false;
    }

    void Association::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "name", name);
        field_to_json(Obj, "macAddress", macAddress);
        field_to_json(Obj, "rssi", rssi);
        field_to_json(Obj, "power", power);
        field_to_json(Obj, "ipv4", ipv4);
        field_to_json(Obj, "ipv6", ipv6);
        field_to_json(Obj, "tx", tx);
        field_to_json(Obj, "rx", rx);
    }

    bool Association::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "name", name);
            field_from_json(Obj, "macAddress", macAddress);
            field_from_json(Obj, "rssi", rssi);
            field_from_json(Obj, "power", power);
            field_from_json(Obj, "ipv4", ipv4);
            field_from_json(Obj, "ipv6", ipv6);
            field_from_json(Obj, "tx", tx);
            field_from_json(Obj, "rx", rx);
            return true;
        } catch (...) {
        }
        return false;
    }

    void AssociationList::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "associations", associations);
    }

    bool AssociationList::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "associations", associations);
            return true;
        } catch (...) {
        }
        return false;
    }

    void Client::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "macAddress", macAddress);
        field_to_json(Obj, "speed", speed);
        field_to_json(Obj, "mode", mode);
        field_to_json(Obj, "ipv4", ipv4);
        field_to_json(Obj, "ipv6", ipv6);
        field_to_json(Obj, "tx", tx);
        field_to_json(Obj, "rx", rx);
    }

    bool Client::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "macAddress", macAddress);
            field_from_json(Obj, "speed", speed);
            field_from_json(Obj, "mode", mode);
            field_from_json(Obj, "ipv4", ipv4);
            field_from_json(Obj, "ipv6", ipv6);
            field_from_json(Obj, "tx", tx);
            field_from_json(Obj, "rx", rx);
            return true;
        } catch (...) {
        }
        return false;
    }

    void ClientList::to_json(Poco::JSON::Object &Obj) const {
        field_to_json(Obj, "clients", clients);
    }

    bool ClientList::from_json(const Poco::JSON::Object::Ptr &Obj) {
        try {
            field_from_json(Obj, "clients", clients);
            return true;
        } catch (...) {
        }
        return false;
    }

}