//
// Created by stephane bourque on 2021-12-01.
//

#include "RESTAPI_mfa_handler.h"
#include "framework/API_Proxy.h"
#include "framework/MicroServiceNames.h"

namespace OpenWifi {

    void RESTAPI_mfa_handler::DoPut() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/submfa");
    }

    void RESTAPI_mfa_handler::DoGet() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/submfa");
    }

}