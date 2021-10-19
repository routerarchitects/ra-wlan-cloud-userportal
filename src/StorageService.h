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
#include "Poco/URI.h"
#include "SubSystemServer.h"

#include "Storage.h"

namespace OpenWifi {

    class Storage : public SubSystemServer, Poco::Runnable {
        public:
            static Storage *instance() {
                if (instance_ == nullptr) {
                    instance_ = new Storage;
                }
                return instance_;
            }

            int 	Start() override;
            void 	Stop() override;

            void run() final;

          private:
            static Storage      								*instance_;
            std::unique_ptr<Poco::Data::SessionPool>        	Pool_;
            std::unique_ptr<Poco::Data::SQLite::Connector>  	SQLiteConn_;
            std::unique_ptr<Poco::Data::PostgreSQL::Connector>  PostgresConn_;
            std::unique_ptr<Poco::Data::MySQL::Connector>       MySQLConn_;
            DBType                                              dbType_ = sqlite;

            typedef std::function<bool(const char *FieldName, std::string &Value)>   exist_func;
            typedef std::function<bool(const char *FieldName, std::string &Value, std::string &Name, std::string &Description)>   expand_func;
            std::map<std::string, exist_func>                   ExistFunc_;
            std::map<std::string, expand_func>                  ExpandFunc_;

            Poco::Thread                                        Updater_;
            std::set<std::string>                               DeviceTypes_;
            std::atomic_bool                                    Running_=false;

            bool UpdateDeviceTypes();
            Storage() noexcept:
                SubSystemServer("Storage", "STORAGE-SVR", "storage")
                {
                }

            int 	Setup_SQLite();
            int 	Setup_MySQL();
            int 	Setup_PostgreSQL();
   };

   inline Storage * Storage() { return Storage::instance(); }

}  // namespace

#endif //UCENTRAL_USTORAGESERVICE_H
