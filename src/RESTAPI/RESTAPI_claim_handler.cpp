//
// Created by stephane bourque on 2022-01-12.
//

#include "RESTAPI_claim_handler.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"

namespace OpenWifi {

    void RESTAPI_claim_handler::DoPut() {
        std::string SerialNumber, Id;

        if(!HasParameter("serialNumber",SerialNumber) || !HasParameter("id",Id)) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        if(!Utils::ValidSerialNumber(SerialNumber)) {
            return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
        }

        std::string EndPoint{"/api/v1/inventory"};
        Poco::JSON::Object  Body;
        OpenAPIRequestPut  API(uSERVICE_PROVISIONING, EndPoint,
                                { { "serialNumber" , SerialNumber },
                                  { "claimer" , UserInfo_.userinfo.id }},
                                  Body, 20000);

        Poco::JSON::Object::Ptr CallResponse;

        auto StatusCode = API.Do(CallResponse, UserInfo_.userinfo.id);

        if(StatusCode == Poco::Net::HTTPServerResponse::HTTP_OK) {
            return ReturnObject(*CallResponse);
        } else {
            return ReturnStatus(StatusCode);
        }
    }

}