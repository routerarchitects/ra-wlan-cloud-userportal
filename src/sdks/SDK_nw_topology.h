/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#pragma once

#include "Poco/JSON/Object.h"
#include "Poco/Net/HTTPServerResponse.h"

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi::SDK::Topology {
	bool Get(RESTAPIHandler *client, const std::string &boardId,
			 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
			 Poco::JSON::Object::Ptr &CallResponse);
} // namespace OpenWifi::SDK::Topology
