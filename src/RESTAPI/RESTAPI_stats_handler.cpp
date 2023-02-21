//
// Created by stephane bourque on 2022-03-10.
//

#include "RESTAPI_stats_handler.h"
#include "RESTObjects/RESTAPI_SubObjects.h"
#include "StatsSvr.h"
#include "StorageService.h"

namespace OpenWifi {
	void RESTAPI_stats_handler::DoGet() {
		auto MAC = GetBinding("mac", "");

		if (MAC.empty() || !Utils::ValidSerialNumber(MAC)) {
			return BadRequest(RESTAPI::Errors::InvalidSerialNumber);
		}

		SubObjects::SubscriberInfo SI;
		if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI)) {
			return NotFound();
		}

		SubObjects::StatsBlock SB;
		for (const auto &device : SI.accessPoints.list) {
			if (device.macAddress == MAC) {
				StatsSvr()->Get(device.serialNumber, SB);
				break;
			}
		}
		Poco::JSON::Object Answer;
		SB.to_json(Answer);
		return ReturnObject(Answer);
	}
} // namespace OpenWifi
