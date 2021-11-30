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

        inline bool GetSubInfo(const std::string &Id, std::shared_ptr<SubObjects::SubscriberInfo> & SubInfo) {
            return false;
        }

    private:
        Poco::ExpireLRUCache<std::string,std::shared_ptr<SubObjects::SubscriberInfo>>    subs_;

        SubscriberCache() noexcept:
        SubSystemServer("SubscriberCache", "SUb-CACHE", "subcache")
            {
            }

    };

    inline class SubscriberCache * SubscriberCache() { return SubscriberCache::instance(); }

}