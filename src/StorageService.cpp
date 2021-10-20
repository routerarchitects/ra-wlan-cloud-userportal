//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include "Poco/Util/Application.h"
#include "Poco/Net/HTTPResponse.h"
#include "Poco/JSON/Parser.h"

#include "StorageService.h"
#include "Daemon.h"
#include "framework/Utils.h"
#include "framework/OpenAPIRequest.h"
#include "RESTAPI/RESTAPI_ProvObjects.h"

namespace OpenWifi {

	class Storage *Storage::instance_ = nullptr;

    int Storage::Start() {
		std::lock_guard		Guard(Mutex_);

		Logger_.setLevel(Poco::Message::PRIO_NOTICE);
        Logger_.notice("Starting.");
        std::string DBType = Daemon()->ConfigGetString("storage.type");

        if (DBType == "sqlite") {
            Setup_SQLite();
        } else if (DBType == "postgresql") {
            Setup_PostgreSQL();
        } else if (DBType == "mysql") {
            Setup_MySQL();
        }

        Updater_.start(*this);

        return 0;
    }

    void Storage::run() {
	    Running_ = true ;
	    bool FirstRun=true;
	    long Retry = 2000;
	    while(Running_) {
	        if(!FirstRun)
	            Poco::Thread::trySleep(Retry);
	        if(!Running_)
	            break;
	    }
	}


    void Storage::Stop() {
	    Running_=false;
	    Updater_.wakeUp();
	    Updater_.join();
        Logger_.notice("Stopping.");
    }
}

// namespace