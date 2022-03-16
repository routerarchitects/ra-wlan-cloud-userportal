//
// Created by stephane bourque on 2022-03-04.
//

#include "SDK_fms.h"

namespace OpenWifi::SDK::FMS {

    namespace Firmware {
        bool GetDeviceInformation(RESTAPIHandler *client, const Types::UUID_t & SerialNumber, FMSObjects::DeviceInformation & DI) {
            OpenAPIRequestGet	Req(    uSERVICE_FIRMWARE,
                                        "/api/v1/deviceInformation/" + SerialNumber,
                                         {},
                                         10000);

            Poco::JSON::Object::Ptr Response;
            auto StatusCode = Req.Do(Response,client== nullptr ? "" : client->UserInfo_.webtoken.access_token_);
            if( StatusCode == Poco::Net::HTTPResponse::HTTP_OK) {
                return DI.from_json(Response);
            }
            return false;
        }
    }

}