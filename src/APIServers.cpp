//
// Created by stephane bourque on 2021-10-23.
//

#include "framework/MicroService.h"
#include "RESTAPI/RESTAPI_homeDeviceMode_handler.h"
#include "RESTAPI/RESTAPI_internetConnection_handler.h"
#include "RESTAPI/RESTAPI_ipReservations_handler.h"
#include "RESTAPI/RESTAPI_subscriberDevices_handler.h"
#include "RESTAPI/RESTAPI_wifiClients_handler.h"
#include "RESTAPI/RESTAPI_wifiNetworks_handler.h"
#include "RESTAPI//RESTAPI_wiredClients_handler.h"
#include "RESTAPI/RESTAPI_accessPoints_handler.h"
#include "RESTAPI/RESTAPI_subscriber_handler.h"
#include "RESTAPI/RESTAPI_oauth2_handler.h"

namespace OpenWifi {

    Poco::Net::HTTPRequestHandler * RESTAPI_external_server(const char *Path, RESTAPIHandler::BindingMap &Bindings,
                                                            Poco::Logger & L, RESTAPI_GenericServer & S) {
        return  RESTAPI_Router<
                RESTAPI_homeDeviceMode_handler,
                RESTAPI_internetConnection_handler,
                RESTAPI_ipReservations_handler,
                RESTAPI_subscriberDevices_handler,
                RESTAPI_wifiClients_handler,
                RESTAPI_wifiNetworks_handler,
                RESTAPI_wiredClients_handler,
                RESTAPI_accessPoints_handler,
                RESTAPI_subscriber_handler,
                RESTAPI_oauth2_handler,
                RESTAPI_system_command>(Path, Bindings, L, S);
    }

    Poco::Net::HTTPRequestHandler * RESTAPI_internal_server(const char *Path, RESTAPIHandler::BindingMap &Bindings,
                                                            Poco::Logger & L, RESTAPI_GenericServer & S) {
        return  RESTAPI_Router_I<
                RESTAPI_system_command>(Path, Bindings, L, S);
    }

}