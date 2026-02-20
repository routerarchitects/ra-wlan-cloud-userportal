//
// Created by stephane bourque on 2021-11-30.
//

#pragma once

#include "framework/RESTAPI_Handler.h"
#include "RESTObjects/RESTAPI_ProvObjects.h"

namespace OpenWifi {
	struct ActionContext;

	class RESTAPI_action_handler : public RESTAPIHandler {
	  public:
		RESTAPI_action_handler(const RESTAPIHandler::BindingMap &bindings, Poco::Logger &L,
							   RESTAPI_GenericServerAccounting &Server, uint64_t TransactionId,
							   bool Internal)
			: RESTAPIHandler(bindings, L,
							 std::vector<std::string>{Poco::Net::HTTPRequest::HTTP_POST,
													  Poco::Net::HTTPRequest::HTTP_OPTIONS},
							 Server, TransactionId, Internal, true, false, RateLimit{}, true) {}

		static auto PathName() { return std::list<std::string>{"/api/v1/action"}; };

		inline bool RoleIsAuthorized(std::string &Reason) {
			if (UserInfo_.userinfo.userRole != SecurityObjects::USER_ROLE::SUBSCRIBER) {
				Reason = "User must be a subscriber";
				return false;
			}
			return true;
		}

		void DoGet() final{};
		void DoPost() final;
		void DoPut() final{};
		void DoDelete() final{};

	  private:
		bool ParseRequest(ActionContext &ctx);
		bool FetchSubscriberDevices(ActionContext &ctx);
		bool FindGatewaySerial(ActionContext &ctx);
		bool ResolveTargetSerial(ActionContext &ctx);
		bool ExecuteConfigure(ActionContext &ctx);
		bool ExecuteCommand(ActionContext &ctx);
		void ReturnSDKResponse(Poco::Net::HTTPResponse::HTTPStatus status,
							   const Poco::JSON::Object::Ptr &response);
	};
} // namespace OpenWifi
