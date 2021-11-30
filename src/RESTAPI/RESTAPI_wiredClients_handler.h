//
// Created by stephane bourque on 2021-10-26.
//

#ifndef OWSUB_RESTAPI_WIREDCLIENTS_HANDLER_H
#define OWSUB_RESTAPI_WIREDCLIENTS_HANDLER_H

#include "framework/MicroService.h"

namespace OpenWifi {
    class RESTAPI_wiredClients_handler : public RESTAPIHandler {
    public:
        RESTAPI_wiredClients_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L, RESTAPI_GenericServer & Server, bool Internal)
        : RESTAPIHandler(bindings, L,
                         std::vector<std::string>{
            Poco::Net::HTTPRequest::HTTP_GET,
            Poco::Net::HTTPRequest::HTTP_OPTIONS},
            Server,
            Internal,true, false, RateLimit{.Interval=1000,.MaxCalls=10}, true){}

            static const std::list<const char *> PathName() { return std::list<const char *>{"/api/v1/wiredClients"}; };

        inline bool RoleIsAuthorized(std::string & Reason) {
            if(UserInfo_.userinfo.userRole != SecurityObjects::USER_ROLE::SUBSCRIBER) {
                Reason = "User must be a subscriber";
                return false;
            }
            return true;
        }

        void DoGet() final;
        void DoPost() final {};
        void DoPut() final {};
        void DoDelete() final {};
    private:
    };
}

#endif //OWSUB_RESTAPI_WIREDCLIENTS_HANDLER_H
