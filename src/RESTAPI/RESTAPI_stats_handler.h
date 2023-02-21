//
// Created by stephane bourque on 2022-03-10.
//

//
// Created by stephane bourque on 2021-11-07.
//

#pragma once

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {
	class RESTAPI_stats_handler : public RESTAPIHandler {
	  public:
		RESTAPI_stats_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
							  RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
							  bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_GET,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal, true, false,
							 RateLimit{.Interval = 1000, .MaxCalls = 10}, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/stats/{mac}"}; };

	  private:
		void DoGet() final;
		void DoPost() final{};
		void DoPut() final{};
		void DoDelete() final{};
	};
} // namespace OpenWifi
