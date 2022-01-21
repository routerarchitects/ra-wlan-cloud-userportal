//
// Created by stephane bourque on 2021-12-13.
//

#pragma once

#include "framework/MicroService.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {
    class ConfigMaker {
    public:
        explicit ConfigMaker(const std::string &Id) :
                id_(Id) {
        }
        bool Prepare();
        bool Push();
    private:
        const std::string   id_;
        bool                bad_=false;
    };
}
