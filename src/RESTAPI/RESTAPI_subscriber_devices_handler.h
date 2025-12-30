//
// Created by stephane bourque on 2021-11-07.
//

#pragma once

#include "framework/RESTAPI_Handler.h"

namespace OpenWifi {
	
	struct AddDeviceContext;
	struct DeleteDeviceContext;

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
	  	bool ADD_DEVICE_VALIDATE_INPUTS(AddDeviceContext &ctx);
		bool ADD_DEVICE_VALIDATE_INVENTORY_OWNERSHIP(AddDeviceContext &ctx);
		bool ADD_DEVICE_LOAD_SUBSCRIBER_INFO(AddDeviceContext &ctx);
		bool ADD_DEVICE_SETUP_GATEWAY(AddDeviceContext &ctx);
		bool ADD_DEVICE_SETUP_MESH(AddDeviceContext &ctx);
		bool ADD_DEVICE_UPDATE_DB(AddDeviceContext &ctx);

        bool DELETE_DEVICE_VALIDATE_INPUTS(DeleteDeviceContext &ctx);
        bool DELETE_DEVICE_VALIDATE_OWNERSHIP(DeleteDeviceContext &ctx);
        bool DELETE_DEVICE_LOAD_SUBINFO_AND_SET_RESET_FLAG(DeleteDeviceContext &ctx);
 		bool DELETE_DEVICE_EXECUTE_GATEWAY_DELETE(DeleteDeviceContext &ctx);
    	bool DELETE_DEVICE_EXECUTE_MESH_DELETE(DeleteDeviceContext &ctx);
		bool DELETE_DEVICE_DELETE_FROM_ALL_DATABASES(const std::string &mac);

	};
} // namespace OpenWifi
