//
// Created by stephane bourque on 2021-11-29.
//

#include "SubscriberCache.h"
#include "StorageService.h"

namespace OpenWifi {

	int SubscriberCache::Start() { return 0; }

	void SubscriberCache::Stop() {}

	bool SubscriberCache::GetSubInfo(const std::string &Id,
									 Poco::SharedPtr<SubObjects::SubscriberInfo> &SubInfo) {
		if (SubsCache_.has(Id)) {
			SubInfo = SubsCache_.get(Id);
			return true;
		}
		SubObjects::SubscriberInfo Sub;
		if (StorageService()->SubInfoDB().GetRecord("id", Id, Sub)) {
			SubsCache_.update(Id, Sub);
			SubInfo = SubsCache_.get(Id);
			return true;
		}
		return false;
	}

	void SubscriberCache::UpdateSubInfo(const std::string &Id,
										const SubObjects::SubscriberInfo &SubInfo) {
		SubsCache_.update(Id, SubInfo);
	}
} // namespace OpenWifi