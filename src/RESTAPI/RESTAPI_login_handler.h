//
// Created by stephane bourque on 2021-11-07.
//

#ifndef OWSUB_RESTAPI_LOGIN_HANDLER_H
#define OWSUB_RESTAPI_LOGIN_HANDLER_H

#include "framework/MicroService.h"

namespace OpenWifi {
    class RESTAPI_login_handler : public RESTAPIHandler {
    public:
        RESTAPI_login_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L, RESTAPI_GenericServer & Server, bool Internal)
        : RESTAPIHandler(bindings, L,
                         std::vector<std::string>{
            Poco::Net::HTTPRequest::HTTP_POST,
            Poco::Net::HTTPRequest::HTTP_OPTIONS},
            Server,
            Internal){}

            static const std::list<const char *> PathName() { return std::list<const char *>{"/api/v1/login"}; };

        void DoGet() final {};
        void DoPost() final;
        void DoPut() final {};
        void DoDelete() final {};
    private:
    };
}


#endif //OWSUB_RESTAPI_LOGIN_HANDLER_H
