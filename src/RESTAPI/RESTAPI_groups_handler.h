/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {
	class RESTAPI_groups_handler : public RESTAPIHandler {
	  public:
		RESTAPI_groups_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
		                       RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
		                       bool Internal)
		    : RESTAPIHandler(bindings, L,
		                     std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
		                                              Poco::Net::HTTPRequest::HTTP_PUT,
		                                              Poco::Net::HTTPRequest::HTTP_DELETE,
		                                              Poco::Net::HTTPRequest::HTTP_OPTIONS},
		                     Server, TransactionId, Internal, true, false, RESTAPIHandler::RateLimit{.Interval = 1000, .MaxCalls = 100}, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/groups/{group_id}"}; }

		void DoGet() override;
		void DoPut() override;
		void DoDelete() override;
		void DoPost() override {}
	};
} // namespace OpenWifi
