//
// Created by stephane bourque on 2021-10-26.
//

#include "RESTAPI_wiredClients_handler.h"

namespace OpenWifi {

    void RESTAPI_wiredClients_handler::DoGet() {
        auto SerialNumber = GetParameter("serialNumber","");
        if(SerialNumber.empty()) {
            return BadRequest(RESTAPI::Errors::MissingSerialNumber);
        }

    }

}