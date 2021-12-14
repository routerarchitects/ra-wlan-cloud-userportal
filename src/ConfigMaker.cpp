//
// Created by stephane bourque on 2021-12-13.
//

#include "ConfigMaker.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StorageService.h"

namespace OpenWifi {

    bool ConfigMaker::Push() {
        return false;
    }

    bool ConfigMaker::Prepare() {
        SubObjects::SubscriberInfo  SI;

        if(!StorageService()->SubInfoDB().GetRecord("id", id_, SI )) {
            bad_ = true;
            return false;
        }



        return false;
    }

}