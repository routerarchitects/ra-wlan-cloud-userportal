//
// Created by stephane bourque on 2022-03-09.
//

#include "StatsSvr.h"
#include "framework/KafkaTopics.h"
#include "framework/KafkaManager.h"
#include "nlohmann/json.hpp"

#define dbg std::cout << __LINE__ << std::endl

namespace OpenWifi {

    int StatsSvr::Start() {
        Running_ = true;
        Types::TopicNotifyFunction F = [this](const std::string &Key, const std::string &Payload) { this->StatsReceived(Key,Payload); };
        StatsWatcherId_ = KafkaManager()->RegisterTopicWatcher(KafkaTopics::STATE, F);
        Worker_.start(*this);
        return 0;
    }

    void StatsSvr::Stop() {
        Running_ = false;
        KafkaManager()->UnregisterTopicWatcher(KafkaTopics::CONNECTION, StatsWatcherId_);
        Queue_.wakeUpAll();
        Worker_.join();
    }

    void StatsSvr::run() {
        Utils::SetThreadName("stats-svr");
        Poco::AutoPtr<Poco::Notification>	Note(Queue_.waitDequeueNotification());
        while(Note && Running_) {
            auto Msg = dynamic_cast<Stats_Msg *>(Note.get());
            if(Msg!= nullptr) {
                try {
                    // std::cout << "Size:" << Queue_.size() << "  " << Msg->Key() << std::endl;
                    // std::cout << Msg->Payload() << std::endl;
                    dbg;
                    nlohmann::json msg = nlohmann::json::parse(Msg->Payload());
                    if (msg.contains(uCentralProtocol::PAYLOAD)) {
                        dbg;
                        auto payload = msg[uCentralProtocol::PAYLOAD];
                        dbg;
                        if (payload.contains("state") && payload.contains("serial")) {
                            dbg;
                            auto serialNumber = payload["serial"].get<std::string>();
                            auto state = payload["state"];
                            dbg;
                            if (state.contains("version")) {
                                dbg;
                                if (state.contains("unit")) {
                                    dbg;
                                    auto unit = state["unit"];
                                    dbg;
                                    if (unit.contains("localtime")) {
                                        dbg;
                                        uint64_t timestamp = unit["localtime"];
                                        dbg;
                                        if (state.contains("interfaces")) {
                                            dbg;
                                            if (state["interfaces"].is_array()) {
                                                dbg;
                                                auto interfaces = state["interfaces"];
                                                auto serial_int = Utils::SerialNumberToInt(serialNumber);
                                                uint64_t int_rx=0, int_tx=0, ext_rx=0,ext_tx=0;
                                                for (const auto &cur_int: interfaces) {
                                                    dbg;
                                                    bool external_stats=true;
                                                    if(cur_int.contains("location")) {
                                                        dbg;
                                                        auto location = cur_int["location"].get<std::string>();
                                                        auto parts = Poco::StringTokenizer(location,"/");
                                                        dbg;
                                                        if(parts.count()==3) {
                                                            if(parts[2]=="0")
                                                                external_stats = true;
                                                            else
                                                                external_stats = false;
                                                        }
                                                    }

                                                    if (cur_int.contains("counters") && cur_int["counters"].contains("rx_bytes") && cur_int["counters"].contains("tx_bytes")) {
                                                        dbg;
                                                        if(external_stats) {
                                                            dbg;
                                                            ext_rx = cur_int["counters"]["rx_bytes"].get<uint64_t>();
                                                            ext_tx = cur_int["counters"]["tx_bytes"].get<uint64_t>();
                                                        } else {
                                                            dbg;
                                                            int_rx = cur_int["counters"]["rx_bytes"].get<uint64_t>();
                                                            int_tx = cur_int["counters"]["tx_bytes"].get<uint64_t>();
                                                        }
                                                    }
                                                }
                                                {
                                                    std::lock_guard G(Mutex_);
                                                    auto it = DeviceStats_.find(serial_int);
                                                    if (it == end(DeviceStats_)) {
                                                        DeviceStats D;
                                                        dbg;
                                                        D.AddValue(timestamp, ext_tx, ext_rx, int_tx, int_rx);
                                                        dbg;
                                                        DeviceStats_[serial_int] = D;
                                                        dbg;
                                                        std::cout << "Created device stats entries for " << serialNumber
                                                                  << std::endl;
                                                        dbg;
                                                    } else {
                                                        dbg;
                                                        it->second.AddValue(timestamp, ext_tx, ext_rx, int_tx, int_rx);
                                                        dbg;
                                                        std::cout << "Adding device stats entries for " << serialNumber
                                                                  << std::endl;
                                                    }
                                                    // DeviceStats_[serial_int].print();
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } catch (const Poco::Exception &E) {
                    Logger().log(E);
                } catch (...) {

                }
            } else {

            }
            dbg;
            Note = Queue_.waitDequeueNotification();
            dbg;
        }
    }

}