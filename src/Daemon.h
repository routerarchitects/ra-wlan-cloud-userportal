//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//

#pragma once

#include <array>
#include <iostream>
#include <cstdlib>
#include <vector>
#include <set>

#include "framework/MicroService.h"
#include "framework/MicroServiceNames.h"
#include "Dashboard.h"

namespace OpenWifi {

	[[maybe_unused]] static const char * vDAEMON_PROPERTIES_FILENAME = "owsub.properties";
    [[maybe_unused]] static const char * vDAEMON_ROOT_ENV_VAR = "OWSUB_ROOT";
    [[maybe_unused]] static const char * vDAEMON_CONFIG_ENV_VAR = "OWSUB_CONFIG";
    [[maybe_unused]] static const char * vDAEMON_APP_NAME = uSERVICE_SUBCRIBER.c_str() ;
    [[maybe_unused]] static const uint64_t vDAEMON_BUS_TIMER = 10000;

    class Daemon : public MicroService {
		public:
			explicit Daemon(const std::string & PropFile,
							const std::string & RootEnv,
							const std::string & ConfigEnv,
							const std::string & AppName,
						  	uint64_t 	BusTimer,
							const SubSystemVec & SubSystems) :
				MicroService( PropFile, RootEnv, ConfigEnv, AppName, BusTimer, SubSystems) {};

            void PostInitialization(Poco::Util::Application &self);
			static Daemon *instance();
			inline OpenWifi::SubDashboard & GetDashboard() { return DB_; }
			Poco::Logger & Log() { return Poco::Logger::get(AppName()); }
	  	private:
			static Daemon 				*instance_;
			OpenWifi::SubDashboard		DB_{};

    };

	inline Daemon * Daemon() { return Daemon::instance(); }
    void DaemonPostInitialization(Poco::Util::Application &self);
}
