//
// Created by stephane bourque on 2021-10-23.
//

#include "framework/MicroService.h"
#include "RESTAPI/RESTAPI_wifiClients_handler.h"
#include "RESTAPI//RESTAPI_wiredClients_handler.h"
#include "RESTAPI/RESTAPI_subscriber_handler.h"
#include "RESTAPI/RESTAPI_oauth2_handler.h"
#include "RESTAPI/RESTAPI_action_handler.h"
#include "RESTAPI/RESTAPI_mfa_handler.h"
#include "RESTAPI/RESTAPI_claim_handler.h"
#include "RESTAPI/RESTAPI_signup_handler.h"
#include "RESTAPI/RESTAPI_stats_handler.h"

namespace OpenWifi {

    Poco::Net::HTTPRequestHandler * RESTAPI_ExtRouter(const char *Path, RESTAPIHandler::BindingMap &Bindings,
                                                            Poco::Logger & L, RESTAPI_GenericServer & S,
                                                            uint64_t TransactionId) {
        return  RESTAPI_Router<
                    RESTAPI_wifiClients_handler,
                    RESTAPI_wiredClients_handler,
                    RESTAPI_subscriber_handler,
                    RESTAPI_oauth2_handler,
                    RESTAPI_action_handler,
                    RESTAPI_mfa_handler,
                    RESTAPI_claim_handler,
                    RESTAPI_signup_handler,
                    RESTAPI_system_command,
                    RESTAPI_stats_handler
                >(Path, Bindings, L, S, TransactionId);
    }

    Poco::Net::HTTPRequestHandler * RESTAPI_IntRouter(const char *Path, RESTAPIHandler::BindingMap &Bindings,
                                                            Poco::Logger & L, RESTAPI_GenericServer & S,
                                                            uint64_t TransactionId) {
        return  RESTAPI_Router_I<
                    RESTAPI_wifiClients_handler,
                    RESTAPI_wiredClients_handler,
                    RESTAPI_subscriber_handler,
                    RESTAPI_oauth2_handler,
                    RESTAPI_action_handler,
                    RESTAPI_mfa_handler,
                    RESTAPI_claim_handler,
                    RESTAPI_signup_handler,
                    RESTAPI_system_command,
                    RESTAPI_stats_handler
                >(Path, Bindings, L, S, TransactionId);
    }

}