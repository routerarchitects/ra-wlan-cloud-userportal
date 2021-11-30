//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#ifndef UCENTRAL_USTORAGESERVICE_H
#define UCENTRAL_USTORAGESERVICE_H

#include <map>

#include "Poco/Data/Session.h"
#include "Poco/Data/SessionPool.h"
#include "Poco/Data/SQLite/Connector.h"
#include "Poco/Data/PostgreSQL/Connector.h"
#include "Poco/Data/MySQL/Connector.h"

#include "framework/MicroService.h"
#include "framework/StorageClass.h"

#include "storage/storage_subscriber_info.h"

namespace OpenWifi {

    class Storage : public StorageClass {
        public:
            static Storage *instance() {
                if (instance_ == nullptr) {
                    instance_ = new Storage;
                }
                return instance_;
            }

            int Start() override;
            void Stop() override;

          private:
            static Storage      								*instance_;
            std::unique_ptr<OpenWifi::SubscriberInfoDB>         SubscriberDB_;
            Poco::Thread                                        Updater_;
            std::atomic_bool                                    Running_=false;
    };

    inline class Storage * StorageService() { return Storage::instance(); }

}  // namespace

#endif //UCENTRAL_USTORAGESERVICE_H
