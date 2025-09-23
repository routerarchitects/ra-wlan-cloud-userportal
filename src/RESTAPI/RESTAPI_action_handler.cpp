//
// Created by stephane bourque on 2021-11-30.
//

#include "RESTAPI_action_handler.h"
#include "ConfigMaker.h"
#include "StorageService.h"
#include "SubscriberCache.h"
#include "sdks/SDK_gw.h"

namespace OpenWifi {

	void RESTAPI_action_handler::DoPost() {
		auto Command = GetParameter("action", "");

		if (Command.empty()) {
			return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
		}

		const auto &Body = ParsedBody_;
		std::string Mac, ImageName, Pattern{"blink"};
		AssignIfPresent(Body, "mac", Mac);

		Poco::toLowerInPlace(Mac);
		Poco::trimInPlace(Mac);
		if (Mac.empty()) {
			return BadRequest(RESTAPI::Errors::MissingSerialNumber);
		}
		uint64_t When = 0, Duration = 30;
		bool keepRedirector = true;
		AssignIfPresent(Body, "when", When);
		AssignIfPresent(Body, "duration", Duration);
		AssignIfPresent(Body, "uri", ImageName);
		AssignIfPresent(Body, "pattern", Pattern);
		AssignIfPresent(Body, "keepRedirector", keepRedirector);

		Poco::SharedPtr<SubObjects::SubscriberInfo> SubInfo;
		auto UserFound = SubscriberCache()->GetSubInfo(UserInfo_.userinfo.id, SubInfo);
		if (!UserFound) {
			SubObjects::SubscriberInfo SI;
			if (!StorageService()->SubInfoDB().GetRecord("id", UserInfo_.userinfo.id, SI))
				return BadRequest(RESTAPI::Errors::SubNoDeviceActivated);
		}

		for (const auto &i : SubInfo->accessPoints.list) {
			if (i.macAddress == Mac) {
				if (Command == "reboot") {
					return SDK::GW::Device::Reboot(this, i.serialNumber, When);
				} else if (Command == "blink") {
					return SDK::GW::Device::LEDs(this, i.serialNumber, When, Duration, Pattern);
				} else if (Command == "upgrade") {
					return SDK::GW::Device::Upgrade(this, i.serialNumber, When, i.latestFirmwareURI,
													keepRedirector);
				} else if (Command == "factory") {
					return SDK::GW::Device::Factory(this, i.serialNumber, When, keepRedirector);
				} else if (Command == "configure") {
					std::string status{};
					auto Response = SDK::GW::Device::SetConfig(this, i.serialNumber, Body, status);
					if (Response != Poco::Net::HTTPServerResponse::HTTP_OK) {
						if (status == "MissingOrInvalidParameters") {
							return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters,"Required Parameters are missing.");
						} else if (status == "DeviceNotConnected") {
							return BadRequest(RESTAPI::Errors::DeviceNotConnected);
						} else if (status == "SSIDInvalidName") {
							return BadRequest(RESTAPI::Errors::SSIDInvalidName);
						} else if (status == "SSIDInvalidPassword") {
							return BadRequest(RESTAPI::Errors::SSIDInvalidPassword);
						} else if (status == "ConfigNotFound") {
							return BadRequest(RESTAPI::Errors::ConfigNotFound);
						}
						return BadRequest(RESTAPI::Errors::InternalError, status);
					}
					return OK();
				} else {
					return BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
				}
			}
		}
		return NotFound();
	}
} // namespace OpenWifi