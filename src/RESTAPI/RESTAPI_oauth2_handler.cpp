//
// Created by stephane bourque on 2021-11-28.
//

#include "RESTAPI_oauth2_handler.h"
#include "framework/MicroService.h"
#include "framework/API_Proxy.h"

namespace OpenWifi {

    void RESTAPI_oauth2_handler::DoPost() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
    }

    void RESTAPI_oauth2_handler::DoGet() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
    }

    void RESTAPI_oauth2_handler::DoDelete() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
    }

}