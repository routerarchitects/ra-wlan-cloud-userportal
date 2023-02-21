//
// Created by stephane bourque on 2022-03-09.
//

#pragma once

#include "Poco/Notification.h"
#include "Poco/NotificationQueue.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "framework/SubSystemServer.h"
#include "framework/utils.h"

namespace OpenWifi {

	class Stats_Msg : public Poco::Notification {
	  public:
		explicit Stats_Msg(std::string Key, std::string Payload)
			: Key_(std::move(Key)), Payload_(std::move(Payload)) {}
		const std::string &Key() { return Key_; }
		const std::string &Payload() { return Payload_; }

	  private:
		std::string Key_;
		std::string Payload_;
	};

	struct DeviceStats {
		uint64_t LastUpdate_ = 0;
		constexpr static const size_t buffer_size = 20;
		std::array<std::uint64_t, buffer_size> timestamps, ext_txs, ext_rxs, int_txs, int_rxs;
		uint64_t base_int_tx = 0, base_int_rx = 0, base_ext_tx = 0, base_ext_rx = 0;

		uint32_t index_ = 0;
		bool no_base = true;
		void AddValue(uint64_t ts, uint32_t ext_tx, uint32_t ext_rx, uint32_t int_tx,
					  uint32_t int_rx) {
			if (no_base) {
				base_int_rx = int_rx;
				base_int_tx = int_tx;
				base_ext_rx = ext_rx;
				base_ext_tx = ext_tx;
				no_base = false;
				return;
			}

			timestamps[index_] = ts;
			int_txs[index_] = int_tx - base_int_tx;
			base_int_tx = int_tx;
			int_rxs[index_] = int_rx - base_int_rx;
			base_int_rx = int_rx;
			ext_txs[index_] = ext_tx - base_ext_tx;
			base_ext_tx = ext_tx;
			ext_rxs[index_] = ext_rx - base_ext_rx;
			base_ext_rx = ext_rx;
			index_++;

			if (index_ == buffer_size) {
				// move everything down by one...
				std::memmove(&timestamps[0], &timestamps[1],
							 sizeof(timestamps[1]) * buffer_size - 1);
				std::memmove(&ext_txs[0], &ext_txs[1], sizeof(ext_txs[1]) * buffer_size - 1);
				std::memmove(&ext_rxs[0], &ext_rxs[1], sizeof(ext_rxs[1]) * buffer_size - 1);
				std::memmove(&int_txs[0], &int_txs[1], sizeof(int_txs[1]) * buffer_size - 1);
				std::memmove(&int_rxs[0], &int_rxs[1], sizeof(int_rxs[1]) * buffer_size - 1);
				index_--;
			}
			LastUpdate_ = Utils::Now();
		}

		void print() {
			for (size_t i = 0; i < index_; i++) {
				std::cout << "TS: " << timestamps[i] << " ext_tx:" << ext_txs[i]
						  << " ext_rx:" << ext_rxs[i] << " int_tx:" << int_txs[i]
						  << " int_rx:" << int_rxs[i] << std::endl;
			}
		}

		void Get(SubObjects::StatsBlock &Stats) {
			Stats.modified = LastUpdate_;
			for (size_t i = 0; i < index_; i++) {
				Stats.external.push_back({timestamps[i], ext_txs[i], ext_rxs[i]});
				Stats.internal.push_back({timestamps[i], int_txs[i], int_rxs[i]});
			}
		}
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

		inline void StatsReceived(const std::string &Key, const std::string &Payload) {
			std::lock_guard G(Mutex_);
			// Logger().information(fmt::format("Device({}): Connection/Ping message.", Key));
			Queue_.enqueueNotification(new Stats_Msg(Key, Payload));
		}

		inline void Get(const std::string &SerialNumber, SubObjects::StatsBlock &Stats) {
			std::lock_guard G(Mutex_);

			auto it = DeviceStats_.find(Utils::SerialNumberToInt(SerialNumber));
			if (it == end(DeviceStats_))
				return;
			it->second.Get(Stats);
		}

	  private:
		uint64_t StatsWatcherId_ = 0;
		Poco::NotificationQueue Queue_;
		Poco::Thread Worker_;
		std::atomic_bool Running_ = false;
		std::map<std::uint64_t, DeviceStats> DeviceStats_;

		StatsSvr() noexcept : SubSystemServer("StateSvr", "STATS-SVR", "statscache") {}
	};
	inline auto StatsSvr() { return StatsSvr::instance(); }
} // namespace OpenWifi