/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#include "SDK_nw_topology.h"

#include "Poco/Logger.h"
#include "Poco/URI.h"

#include "framework/MicroServiceFuncs.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"

namespace OpenWifi::SDK::Topology {
	bool Get(RESTAPIHandler *client, const std::string &boardId,
			 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
			 Poco::JSON::Object::Ptr &CallResponse) {

		const std::string EndPoint = "/api/v1/topology";
		auto API = OpenAPIRequestGet(uSERVICE_NETWORK_TOPOLOGY, EndPoint, {{"boardId", boardId}}, 60000);
		CallStatus = API.Do(CallResponse,
							client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
		return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
	}
} // namespace OpenWifi::SDK::Topology
