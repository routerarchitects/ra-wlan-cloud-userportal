/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include "framework/RESTAPI_Handler.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {

	struct AddDeviceContext;

	class RESTAPI_subscriber_devices_handler : public RESTAPIHandler {
	  public:
		RESTAPI_subscriber_devices_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
								   RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
								   bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_DELETE,
													  Poco::Net::HTTPRequest::HTTP_POST,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal, true, false,
							 RateLimit{.Interval = 1000, .MaxCalls = 10}, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/subscriber/devices/{mac}"}; };

		void DoGet() final{};
		void DoPost() final;
		void DoPut() final{};
		void DoDelete() final;

	  private:
		bool Validate_Inputs(std::string &mac);
		bool FetchSubscriberDevices(AddDeviceContext &ctx);
		bool IsDeviceAlreadyProvisioned(const AddDeviceContext &ctx);
		bool IsDevicePresentInInventory(const AddDeviceContext &ctx);
		void InitializeSubscriberDevice(AddDeviceContext &ctx);
		bool PrepareGatewayConfiguration(AddDeviceContext &ctx);
		bool PrepareMeshConfiguration(AddDeviceContext &ctx);
		bool CreateProvisioningRecord(AddDeviceContext &ctx);
	};
} // namespace OpenWifi
