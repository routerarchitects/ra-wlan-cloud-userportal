//
// Created by stephane bourque on 2021-12-01.
//

#include "RESTAPI_mfa_handler.h"
#include "framework/API_Proxy.h"

namespace OpenWifi {

    void RESTAPI_mfa_handler::DoPut() {
        std::cout << "DoPut" << std::endl;
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/submfa");
        std::cout << "DoPut" << std::endl;
    }

    void RESTAPI_mfa_handler::DoGet() {
        std::cout << "DoGet" << std::endl;
        API_Proxy(Logger_, Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/submfa");
        std::cout << "DoGet" << std::endl;
    }

}