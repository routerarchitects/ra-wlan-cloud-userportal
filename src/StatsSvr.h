//
// Created by stephane bourque on 2022-03-09.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {

    class Stats_Msg : public Poco::Notification {
    public:
        explicit Stats_Msg(const std::string &Key, const std::string &Payload ) :
                Key_(Key),
                Payload_(Payload) {}
        const std::string & Key() { return Key_; }
        const std::string & Payload() { return Payload_; }
    private:
        std::string     Key_;
        std::string     Payload_;
    };

    class StatsSvr : public SubSystemServer, Poco::Runnable {
    public:
        static auto instance() {
            static auto instance_ = new StatsSvr;
            return instance_;
        }

        int Start() override;
        void Stop() override;
        void run() override;

        void StatsReceived( const std::string & Key, const std::string & Payload) {
            std::lock_guard G(Mutex_);
            Logger().information(Poco::format("Device(%s): Connection/Ping message.", Key));
            Queue_.enqueueNotification( new Stats_Msg(Key,Payload));
        }

    private:
        uint64_t                                StatsWatcherId_=0;
        Poco::NotificationQueue                 Queue_;
        Poco::Thread                            Worker_;
        std::atomic_bool                        Running_=false;

        StatsSvr() noexcept:
                SubSystemServer("StateSvr", "STATS-SVR", "statscache")
        {
        }

    };
    inline auto StatsSvr() { return StatsSvr::instance(); }
}