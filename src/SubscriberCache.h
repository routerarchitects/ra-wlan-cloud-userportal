//
// Created by stephane bourque on 2021-11-29.
//

#pragma once

#include "framework/MicroService.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "Poco/ExpireLRUCache.h"

namespace OpenWifi {
    class SubscriberCache : public SubSystemServer {
    public:
        static SubscriberCache *instance() {
            static auto instance_ = new SubscriberCache;
            return instance_;
        }

        int Start() override;
        void Stop() override;

        bool GetSubInfo(const std::string &Id, Poco::SharedPtr<SubObjects::SubscriberInfo> & SubInfo);

    private:
        Poco::ExpireLRUCache<std::string,SubObjects::SubscriberInfo>    SubsCache_{2048};

        SubscriberCache() noexcept:
        SubSystemServer("SubscriberCache", "SUb-CACHE", "subcache")
            {
            }

    };

    inline class SubscriberCache * SubscriberCache() { return SubscriberCache::instance(); }

}