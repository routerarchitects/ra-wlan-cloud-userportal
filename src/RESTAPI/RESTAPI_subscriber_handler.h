/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
//
// Created by stephane bourque on 2021-11-07.
//

#pragma once

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {
	namespace SubObjects {
		struct SubscriberInfo;
	}
	namespace ProvObjects {
		struct SubscriberDeviceList;
		struct SubscriberDevice;
	}

	class RESTAPI_subscriber_handler : public RESTAPIHandler {
	  public:
		RESTAPI_subscriber_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
								   RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
								   bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
													  Poco::Net::HTTPRequest::HTTP_DELETE,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal, true, false,
							 RateLimit{.Interval = 1000, .MaxCalls = 10}, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/subscriber"}; };

		void DoGet() final;
		void DoPost() final{};
		void DoPut() final {};
		void DoDelete() final;

	  private:
		bool ValidateUserInfo();
		bool LoadSubscriberInfo(SubObjects::SubscriberInfo &subInfo);
		bool LoadProvisioningDevices(ProvObjects::SubscriberDeviceList &devices);
		bool PrepareSubInfoObject(SubObjects::SubscriberInfo &subInfo,
								  const ProvObjects::SubscriberDeviceList &devices);
		bool PrepareDefaultConfig(SubObjects::SubscriberInfo &subInfo,
									ProvObjects::SubscriberDevice &subDevice);
		bool LinkSubscriberDevice(const SubObjects::SubscriberInfo &subInfo,
											const ProvObjects::SubscriberDevice &subDevice);
		bool CreateDbEntry(SubObjects::SubscriberInfo &subInfo);
		bool ProvisionSubscriber(SubObjects::SubscriberInfo &subInfo);
		bool DeletePostSubscriber();
	};
} // namespace OpenWifi
