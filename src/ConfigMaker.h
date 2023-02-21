//
// Created by stephane bourque on 2021-12-13.
//

#pragma once

#include "Poco/Logger.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {
	class ConfigMaker {
	  public:
		explicit ConfigMaker(Poco::Logger &L, const std::string &Id) : Logger_(L), id_(Id) {}
		bool Prepare();
		bool Push();

	  private:
		Poco::Logger &Logger_;
		const std::string id_;
		bool bad_ = false;
	};
} // namespace OpenWifi
