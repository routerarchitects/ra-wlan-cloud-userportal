//
// Created by stephane bourque on 2022-02-20.
//

#include "RESTAPI_signup_handler.h"
#include "framework/API_Proxy.h"

namespace OpenWifi {

    void RESTAPI_signup_handler::DoPost() {
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/signup");
    }

}