//
// Created by stephane bourque on 2022-03-04.
//

#pragma once

#include "framework/RESTAPI_Handler.h"
#include "RESTObjects/RESTAPI_FMSObjects.h"

namespace OpenWifi::SDK::FMS {

    namespace Firmware {
        bool GetDeviceInformation(RESTAPIHandler *client, const Types::UUID_t & SerialNumber, FMSObjects::DeviceInformation & DI);
    }

}

