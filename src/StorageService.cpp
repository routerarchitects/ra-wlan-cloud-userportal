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
		std::lock_guard Guard(Mutex_);
		Logger().notice("Starting.");

		StorageClass::Start();
		SubscriberDB_ = std::make_unique<OpenWifi::SubscriberInfoDB>(dbType_, *Pool_, Logger());
		SubscriberDB_->Create();

		return 0;
	}

	void StorageService::Stop() {
		std::lock_guard Guard(Mutex_);

		StorageClass::Stop();
		Logger().notice("Stopping.");
	}
} // namespace OpenWifi

// namespace