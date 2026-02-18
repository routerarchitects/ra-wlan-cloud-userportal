/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2021-11-07.
//
#include "framework/ow_constants.h"

#include "RESTAPI_subscriber_handler.h"
#include "framework/API_Proxy.h"
#include "framework/MicroServiceNames.h"
#include "framework/utils.h"

namespace OpenWifi {
	void RESTAPI_subscriber_handler::DoPost() {
		auto userName = GetParameter("email");
		Poco::toLowerInPlace(userName);
		Poco::trimInPlace(userName);

		auto registrationId = GetParameter("registrationId");
		Poco::toLowerInPlace(registrationId);
		Poco::trimInPlace(registrationId);

		Logger().information(fmt::format(
			"Subscriber signup request for email: {} registrationId: {}.", userName, registrationId));

		if (!Utils::ValidEMailAddress(userName)) {
			return BadRequest(RESTAPI::Errors::InvalidEmailAddress);
		}

		if (registrationId.empty()) {
			return BadRequest(RESTAPI::Errors::InvalidRegistrationOperatorName);
		}

		return API_Proxy(Logger(), Request, Response, uSERVICE_PROVISIONING.c_str(),
						 "/api/v1/subscriber", 60000);
	}

	void RESTAPI_subscriber_handler::DoDelete() {
		bool expired = false, contacted = false;
		if (!IsAuthorized(expired, contacted, true)) {
			if (expired)
				return UnAuthorized(RESTAPI::Errors::EXPIRED_TOKEN);
			if (contacted)
				return UnAuthorized(RESTAPI::Errors::INVALID_TOKEN);
			return UnAuthorized(RESTAPI::Errors::SECURITY_SERVICE_UNREACHABLE);
		}

		if (UserInfo_.userinfo.id.empty()) {
			Logger().warning("Received delete request without id.");
			return NotFound();
		}
		Logger().information(fmt::format("Subscriber delete request for email: {} id: {}.", UserInfo_.userinfo.email, UserInfo_.userinfo.id));
		return OK();
	}
} // namespace OpenWifi
