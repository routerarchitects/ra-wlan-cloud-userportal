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
                    std::cout << "Size:" << Queue_.size() << "  " << Msg->Key() << std::endl;
                    nlohmann::json msg = nlohmann::json::parse(Msg->Payload());
                    if (msg.contains(uCentralProtocol::PAYLOAD)) {
                        auto payload = msg[uCentralProtocol::PAYLOAD];
                        if (payload.contains("version")) {
                            if (payload.contains("unit")) {
                                auto unit = payload["unit"];
                                if (unit.contains("localtime")) {
                                    auto timestamp = unit["localtime"];
                                    if (payload.contains("interfaces")) {
                                        if (payload["interfaces"].is_array()) {
                                            auto interfaces = payload["interfaces"];
                                            for (const auto &cur_int: interfaces) {
                                                if (cur_int.contains("counters")) {

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
            Note = Queue_.waitDequeueNotification();
        }
    }

}