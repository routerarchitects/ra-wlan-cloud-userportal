/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-02-20.
//

#pragma once

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {
	class RESTAPI_topology_handler : public RESTAPIHandler {
	  public:
		RESTAPI_topology_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
							   RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
							   bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal, false, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/topology"}; };

		void DoGet() final;
		void DoPost() final {};
		void DoPut() final {};
		void DoDelete() final {};

	  private:
	};
} // namespace OpenWifi
