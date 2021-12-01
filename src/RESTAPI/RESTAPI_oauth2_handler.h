//
// Created by stephane bourque on 2021-11-28.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {
    class RESTAPI_oauth2_handler : public RESTAPIHandler {
    public:
        RESTAPI_oauth2_handler(const RESTAPIHandler::BindingMap &bindings,
                               Poco::Logger &L,
                               RESTAPI_GenericServer & Server, bool Internal)
        : RESTAPIHandler(bindings, L,
                         std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_POST,
                                                  Poco::Net::HTTPRequest::HTTP_DELETE,
                                                  Poco::Net::HTTPRequest::HTTP_GET,
                                                  Poco::Net::HTTPRequest::HTTP_OPTIONS},
                                                  Server,
                                                  Internal, false, true , RateLimit{.Interval=1000,.MaxCalls=10}) {}
        static const std::list<const char *> PathName() { return std::list<const char *>{"/api/v1/oauth2/{token}","/api/v1/oauth2"}; };

        void DoGet() final;
        void DoPost() final;
        void DoPut() final {};
        void DoDelete() final;
    private:
    };
}

