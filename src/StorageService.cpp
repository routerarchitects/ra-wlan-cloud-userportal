//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#include "StorageService.h"

namespace OpenWifi {

    int StorageService::Start() {
		std::lock_guard		Guard(Mutex_);
		Logger_.notice("Starting.");

		StorageClass::Start();



        std::cout << __FILE_NAME__ << ":" << __func__  << ":" << __LINE__ << " : " << MicroService::instance().DataDir() << std::endl;
		SubscriberDB_ = std::make_unique<OpenWifi::SubscriberInfoDB>(dbType_,*Pool_, Logger_);
		std::cout << __FILE_NAME__ << ":" << __func__  << ":" << __LINE__ << std::endl;
		SubscriberDB_->Create();
		std::cout << __FILE_NAME__ << ":" << __func__  << ":" << __LINE__ << std::endl;

        return 0;
    }

    void StorageService::Stop() {
	    std::lock_guard		Guard(Mutex_);

	    StorageClass::Stop();
        Logger_.notice("Stopping.");
    }
}

// namespace