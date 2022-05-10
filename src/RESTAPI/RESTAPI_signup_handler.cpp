//
// Created by stephane bourque on 2022-02-20.
//

#include "RESTAPI_signup_handler.h"
#include "framework/API_Proxy.h"
#include "framework/MicroService.h"

namespace OpenWifi {

    void RESTAPI_signup_handler::DoPost() {
        //  do dome basic checking before we send this over.
        std::cout << __LINE__ << std::endl;
        auto UserName = GetParameter("email");
        Poco::toLowerInPlace(UserName);
        Poco::trimInPlace(UserName);

        std::cout << __LINE__ << std::endl;
        auto SerialNumber = GetParameter("macAddress");
        Poco::toLowerInPlace(SerialNumber);
        Poco::trimInPlace(SerialNumber);
        std::cout << __LINE__ << std::endl;

        auto registrationId = GetParameter("registrationId");
        Poco::toLowerInPlace(registrationId);
        Poco::trimInPlace(registrationId);
        std::cout << __LINE__ << std::endl;

        if(!Utils::ValidSerialNumber(SerialNumber)) {
            return BadRequest(RESTAPI::Errors::InvalidSerialNumber);
        }
        std::cout << __LINE__ << std::endl;

        if(!Utils::ValidEMailAddress(UserName)) {
            return BadRequest(RESTAPI::Errors::InvalidEmailAddress);
        }
        std::cout << __LINE__ << std::endl;

        if(registrationId.empty()) {
            return BadRequest(RESTAPI::Errors::InvalidRegistrationOperatorName);
        }

        std::cout << __LINE__ << std::endl;

        return API_Proxy(Logger(), Request, Response, ParsedBody_, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoPut() {
        std::cout << __LINE__ << std::endl;
        return API_Proxy(Logger(), Request, Response, ParsedBody_, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoGet() {
        std::cout << __LINE__ << std::endl;
        return API_Proxy(Logger(), Request, Response, ParsedBody_, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

    void RESTAPI_signup_handler::DoDelete() {
        return API_Proxy(Logger(), Request, Response, ParsedBody_, uSERVICE_PROVISIONING.c_str(), "/api/v1/signup", 60000);
    }

}