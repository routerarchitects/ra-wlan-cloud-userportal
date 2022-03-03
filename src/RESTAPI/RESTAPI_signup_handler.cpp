//
// Created by stephane bourque on 2022-02-20.
//

#include "RESTAPI_signup_handler.h"
#include "framework/API_Proxy.h"
#include "framework/MicroService.h"

namespace OpenWifi {

    void RESTAPI_signup_handler::DoPost() {
        //  do dome basic checking before we send this over.
        auto UserName = GetParameter("email","");
        Poco::toLowerInPlace(UserName);
        auto SerialNumber = GetParameter("macAddress","");
        Poco::toLowerInPlace(SerialNumber);

        if(!Utils::ValidSerialNumber(SerialNumber)) {
            return BadRequest(RESTAPI::Errors::InvalidSerialNumber);
        }

        if(!Utils::ValidEMailAddress(UserName)) {
            return BadRequest(RESTAPI::Errors::InvalidEmailAddress);
        }

        return API_Proxy(Logger(), Request, Response, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoPut() {
        return API_Proxy(Logger(), Request, Response, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoGet() {
        return API_Proxy(Logger(), Request, Response, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoDelete() {
        return API_Proxy(Logger(), Request, Response, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

}