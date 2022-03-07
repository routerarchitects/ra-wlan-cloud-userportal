//
// Created by stephane bourque on 2022-01-11.
//

#include "SDK_prov.h"


namespace OpenWifi::SDK::Prov {

    namespace Device {
        bool Get(RESTAPIHandler *client, const std::string &Mac, ProvObjects::InventoryTag & Device) {
            std::string         EndPoint = "/api/v1/inventory/" + Mac ;

            auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
            Poco::JSON::Object::Ptr CallResponse;

            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if(ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                try {
                    return Device.from_json(CallResponse);
                } catch (...) {
                    return false;
                }
            }
            return false;
        }

    }

    namespace Configuration {
        bool Get( RESTAPIHandler *client, const std::string &ConfigUUID, ProvObjects::DeviceConfiguration & Config) {
            std::string         EndPoint = "/api/v1/configurations/" + ConfigUUID ;
            auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
            Poco::JSON::Object::Ptr CallResponse;
            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if(ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                try {
                    return Config.from_json(CallResponse);
                } catch (...) {
                    return false;
                }
            }
            return false;
        }

        bool Delete( RESTAPIHandler *client, const std::string &ConfigUUID) {
            std::string         EndPoint = "/api/v1/configurations/" + ConfigUUID ;
            auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
            Poco::JSON::Object::Ptr CallResponse;
            auto ResponseStatus = API.Do(client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if(ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                return true;
            }
            return false;
        }

        bool Create( RESTAPIHandler *client, const std::string & Mac, const ProvObjects::DeviceConfiguration & Config , std::string & ConfigUUID) {
            std::string         EndPoint = "/api/v1/configurations/0" ;
            Poco::JSON::Object  Body;
            Config.to_json(Body);

            std::stringstream OOS;
            Body.stringify(OOS);

            std::cout << OOS.str() << std::endl;

            auto API = OpenAPIRequestPost(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
            Poco::JSON::Object::Ptr CallResponse;
            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            std::cout << __LINE__ << std::endl;
            if(ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
                std::cout << __LINE__ << std::endl;
                std::ostringstream OS;
                CallResponse->stringify(OS);
                std::cout << "CREATE: " << OS.str() << std::endl;
                return false;
            }

            std::cout << __LINE__ << std::endl;
            ProvObjects::DeviceConfiguration    NewConfig;
            NewConfig.from_json(CallResponse);
            ConfigUUID = NewConfig.info.id;
            std::cout << __LINE__ << std::endl;

            Body.clear();
            Body.set("serialNumber", Mac);
            Body.set("deviceConfiguration", NewConfig.info.id);
            EndPoint = "/api/v1/inventory/" + Mac ;
            auto API2 = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
            CallResponse->clear();
            ResponseStatus = API2.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            std::cout << __LINE__ << std::endl;
            if(ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
                std::cout << __LINE__ << std::endl;
                std::ostringstream OS;
                CallResponse->stringify(OS);
                std::cout << "ASSIGN: " << OS.str() << std::endl;
                return false;
            }
            std::cout << __LINE__ << std::endl;
            return true;
        }

        bool Update( RESTAPIHandler *client, const std::string &ConfigUUID, ProvObjects::DeviceConfiguration & Config) {
            std::cout << __LINE__ << std::endl;
            std::string         EndPoint = "/api/v1/configurations/"+ConfigUUID ;
            Poco::JSON::Object  Body;
            Config.to_json(Body);
            auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, Body, 10000);
            Poco::JSON::Object::Ptr CallResponse;
            std::cout << __LINE__ << std::endl;
            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            std::cout << __LINE__ << std::endl;
            if(ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
                std::cout << __LINE__ << std::endl;
                return false;
            }
            std::cout << __LINE__ << std::endl;
            return true;
        }

        bool Push( RESTAPIHandler *client, const std::string &serialNumber, ProvObjects::InventoryConfigApplyResult &Results ) {
            std::cout << __LINE__ << std::endl;
            std::string         EndPoint = "/api/v1/inventory/"+serialNumber ;
            Poco::JSON::Object  Body;
            auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {
                    { "applyConfiguration", "true" }
                }, 30000);

            Poco::JSON::Object::Ptr CallResponse;
            std::cout << __LINE__ << std::endl;
            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            std::cout << __LINE__ << std::endl;
            if(ResponseStatus == Poco::Net::HTTPResponse::HTTP_OK) {
                Results.from_json(CallResponse);
                return true;
            }
            std::cout << __LINE__ << std::endl;
            return false;
        }
    }

    namespace Subscriber {
        bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId, ProvObjects::InventoryTagList & Devices) {

            std::string         EndPoint = "/api/v1/inventory";
            auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {
                    {"subscriber", SubscriberId}
                }, 60000);
            Poco::JSON::Object::Ptr CallResponse;

            auto ResponseStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if(ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
                try {
                    return Devices.from_json(CallResponse);
                } catch (...) {
                    return false;
                }
            }
            return false;
        }

        bool ReturnDeviceToInventory(RESTAPIHandler *client, const std::string &SubscriberId, const std::string &SerialNumber) {
            return false;
        }
    }

}