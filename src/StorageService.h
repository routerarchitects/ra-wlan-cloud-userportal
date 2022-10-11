//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#pragma once

#include <map>

#include "Poco/Data/Session.h"
#include "Poco/Data/SessionPool.h"
#include "Poco/Data/SQLite/Connector.h"
#include "Poco/Data/PostgreSQL/Connector.h"
#include "Poco/Data/MySQL/Connector.h"

#include "framework/StorageClass.h"

#include "storage/storage_subscriber_info.h"

namespace OpenWifi {

    class StorageService : public StorageClass {
        public:
        static StorageService *instance() {
            static auto instance_ = new StorageService;
                return instance_;
            }

            OpenWifi::SubscriberInfoDB & SubInfoDB() { return *SubscriberDB_; };

            int Start() override;
            void Stop() override;

          private:
            std::unique_ptr<OpenWifi::SubscriberInfoDB>         SubscriberDB_;
            Poco::Thread                                        Updater_;
            std::atomic_bool                                    Running_=false;
    };

    inline class StorageService * StorageService() { return StorageService::instance(); }

}  // namespace
