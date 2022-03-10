//
// Created by stephane bourque on 2022-03-09.
//

#include "StatsSvr.h"

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
        Poco::AutoPtr<Poco::Notification>	Note(Queue_.waitDequeueNotification());
        while(Note && Running_) {
            auto Msg = dynamic_cast<Stats_Msg *>(Note.get());
            if(Msg!= nullptr) {
                try {
                    // std::cout << "Size:" << Queue_.size() << "  " << Msg->Key() << std::endl;
                    // std::cout << Msg->Payload() << std::endl;
                    nlohmann::json msg = nlohmann::json::parse(Msg->Payload());
                    if (msg.contains(uCentralProtocol::PAYLOAD)) {
                        auto payload = msg[uCentralProtocol::PAYLOAD];
                        if (payload.contains("state") && payload.contains("serial")) {
                            auto serialNumber = payload["serial"].get<std::string>();
                            auto state = payload["state"];
                            if (state.contains("version")) {
                                if (state.contains("unit")) {
                                    auto unit = state["unit"];
                                    if (unit.contains("localtime")) {
                                        uint64_t timestamp = unit["localtime"];
                                        uint64_t int_rx=0, int_tx=0, ext_rx=0,ext_tx=0;
                                        if (state.contains("interfaces")) {
                                            if (state["interfaces"].is_array()) {
                                                auto interfaces = state["interfaces"];
                                                auto serial_int = Utils::SerialNumberToInt(serialNumber);
                                                for (const auto &cur_int: interfaces) {
                                                    bool external_stats=true;
                                                    if(cur_int.contains("location")) {
                                                        auto location = cur_int["location"].get<std::string>();
                                                        auto parts = Poco::StringTokenizer(location,"/");
                                                        if(parts.count()==3) {
                                                            if(parts[2]=="0")
                                                                external_stats = true;
                                                            else
                                                                external_stats = false;
                                                        }
                                                    }

                                                    if (cur_int.contains("counters")) {
                                                        if(external_stats) {
                                                            ext_rx = cur_int["counters"]["rx_bytes"].get<uint64_t>();
                                                            ext_tx = cur_int["counters"]["tx_bytes"].get<uint64_t>();
                                                        } else {
                                                            int_rx = cur_int["counters"]["rx_bytes"].get<uint64_t>();
                                                            int_tx = cur_int["counters"]["tx_bytes"].get<uint64_t>();
                                                        }
                                                    }
                                                }
                                                auto it = DeviceStats_.find(serial_int);
                                                if(it==end(DeviceStats_)) {
                                                    DeviceStats D;
                                                    D.AddValue(timestamp,ext_tx,ext_rx,int_tx,int_rx);
                                                    DeviceStats_[serial_int]=D;
                                                    std::cout << "Created device stats entries for " << serialNumber << std::endl;
                                                } else {
                                                    it->second.AddValue(timestamp,ext_tx,ext_rx,int_tx,int_rx);
                                                    std::cout << "Adding device stats entries for " << serialNumber << std::endl;
                                                }
                                                DeviceStats_[serial_int].print();
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
            Note = Queue_.waitDequeueNotification();
        }
    }

}