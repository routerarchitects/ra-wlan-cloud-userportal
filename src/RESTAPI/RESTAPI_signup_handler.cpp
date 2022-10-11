//
// Created by stephane bourque on 2022-02-20.
//

#include "RESTAPI_signup_handler.h"
#include "framework/API_Proxy.h"
#include "framework/MicroServiceNames.h"

namespace OpenWifi {

    void RESTAPI_signup_handler::DoPost() {
        //  do dome basic checking before we send this over.
        auto UserName = GetParameter("email");
        Poco::toLowerInPlace(UserName);
        Poco::trimInPlace(UserName);

        auto SerialNumber = GetParameter("macAddress");
        Poco::toLowerInPlace(SerialNumber);
        Poco::trimInPlace(SerialNumber);

        auto registrationId = GetParameter("registrationId");
        Poco::toLowerInPlace(registrationId);
        Poco::trimInPlace(registrationId);

        if(!Utils::ValidSerialNumber(SerialNumber)) {
            return BadRequest(RESTAPI::Errors::InvalidSerialNumber);
        }

        if(!Utils::ValidEMailAddress(UserName)) {
            return BadRequest(RESTAPI::Errors::InvalidEmailAddress);
        }

        if(registrationId.empty()) {
            return BadRequest(RESTAPI::Errors::InvalidRegistrationOperatorName);
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