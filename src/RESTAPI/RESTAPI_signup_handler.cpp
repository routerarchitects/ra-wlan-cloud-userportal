//
// Created by stephane bourque on 2022-02-20.
//

#include "RESTAPI_signup_handler.h"
#include "framework/API_Proxy.h"

namespace OpenWifi {

    void RESTAPI_signup_handler::DoPost() {
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