//
// Created by stephane bourque on 2021-11-28.
//

#include "RESTAPI_oauth2_handler.h"
#include "framework/MicroService.h"
#include "framework/API_Proxy.h"

namespace OpenWifi {

    void RESTAPI_oauth2_handler::DoPost() {
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
        API_Proxy(Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
    }

    void RESTAPI_oauth2_handler::DoGet() {
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
        API_Proxy(Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
    }

    void RESTAPI_oauth2_handler::DoDelete() {
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
        API_Proxy(Request, Response, OpenWifi::uSERVICE_SECURITY.c_str(),"/api/v1/suboauth2");
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
    }

}