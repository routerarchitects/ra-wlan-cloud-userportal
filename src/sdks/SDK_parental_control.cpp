/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "SDK_parental_control.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"

namespace OpenWifi::SDK::ParentalControl {

	namespace {
		bool IsParsedObjectSuccess(Poco::Net::HTTPResponse::HTTPStatus callStatus,
								   const Poco::JSON::Object::Ptr &callResponse) {
			return callStatus == Poco::Net::HTTPResponse::HTTP_OK && callResponse != nullptr;
		}

		bool IsParsedArraySuccess(Poco::Net::HTTPResponse::HTTPStatus callStatus,
								  const Poco::JSON::Array::Ptr &arrayResponse) {
			return callStatus == Poco::Net::HTTPResponse::HTTP_OK && arrayResponse != nullptr;
		}

		bool ExecuteGetArray(RESTAPIHandler *client, const std::string &endpoint,
							 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							 Poco::JSON::Array::Ptr &ArrayResponse,
							 Poco::JSON::Object::Ptr &ObjectResponse) {
			auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
			CallStatus = API.Do(ArrayResponse, ObjectResponse, "");
			return IsParsedArraySuccess(CallStatus, ArrayResponse);
		}

		bool ExecuteGetObject(RESTAPIHandler *client, const std::string &endpoint,
							  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							  Poco::JSON::Object::Ptr &CallResponse) {
			auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
			CallStatus = API.Do(CallResponse, "");
			return IsParsedObjectSuccess(CallStatus, CallResponse);
		}

		bool ExecutePostObject(RESTAPIHandler *client, const std::string &endpoint,
							   const Poco::JSON::Object &Body, uint64_t TimeoutMs,
							   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							   Poco::JSON::Object::Ptr &CallResponse) {
			auto API = OpenAPIRequestPost(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, TimeoutMs);
			CallStatus = API.Do(CallResponse, "");
			return IsParsedObjectSuccess(CallStatus, CallResponse);
		}

		bool ExecutePutObject(RESTAPIHandler *client, const std::string &endpoint,
							  const Poco::JSON::Object &Body, uint64_t TimeoutMs,
							  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							  Poco::JSON::Object::Ptr &CallResponse) {
			auto API = OpenAPIRequestPut(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, TimeoutMs);
			CallStatus = API.Do(CallResponse, "");
			return IsParsedObjectSuccess(CallStatus, CallResponse);
		}

		bool ExecuteDelete(RESTAPIHandler *client, const std::string &endpoint, uint64_t TimeoutMs,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody) {
			auto API = OpenAPIRequestDelete(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, TimeoutMs);
			CallStatus = API.Do(CallResponse, RawResponseBody, "");
			return CallStatus == Poco::Net::HTTPResponse::HTTP_OK;
		}
	} // namespace

	// =========================================================================
	// Groups
	// =========================================================================

	bool GetGroups(RESTAPIHandler *client, const std::string &SubscriberId,
	               Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	               Poco::JSON::Array::Ptr &ArrayResponse,
	               Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups";
		return ExecuteGetArray(client, endpoint, CallStatus, ArrayResponse, ObjectResponse);
	}

	bool CreateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups";
		return ExecutePostObject(client, endpoint, Body, 60000, CallStatus, CallResponse);
	}

	bool GetGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	              const std::string &GroupId,
	              Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	              Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		return ExecuteGetObject(client, endpoint, CallStatus, CallResponse);
	}

	bool UpdateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId, const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		return ExecutePutObject(client, endpoint, Body, 60000, CallStatus, CallResponse);
	}

	bool DeleteGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse,
	                 std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		if (!ExecuteDelete(client, endpoint, 60000, CallStatus, CallResponse, RawResponseBody)) {
			return false;
		}
		if (RawResponseBody.empty()) {
			return true;
		}
		return CallResponse != nullptr;
	}

	// =========================================================================
	// Schedules
	// =========================================================================

	bool GetSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
					  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					  Poco::JSON::Array::Ptr &ArrayResponse,
					  Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules";
		return ExecuteGetArray(client, endpoint, CallStatus, ArrayResponse, ObjectResponse);
	}

	bool CreateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules";
		return ExecutePostObject(client, endpoint, Body, 60000, CallStatus, CallResponse);
	}

	bool GetSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
					 const std::string &ScheduleId, Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		return ExecuteGetObject(client, endpoint, CallStatus, CallResponse);
	}

	bool UpdateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId, const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		return ExecutePutObject(client, endpoint, Body, 120000, CallStatus, CallResponse);
	}

	bool DeleteSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		if (!ExecuteDelete(client, endpoint, 120000, CallStatus, CallResponse, RawResponseBody)) {
			return false;
		}
		if (RawResponseBody.empty()) {
			return true;
		}
		return CallResponse != nullptr;
	}

	// =========================================================================
	// Group Devices
	// =========================================================================

	bool GetGroupDevices(RESTAPIHandler *client, const std::string &SubscriberId,
						 const std::string &GroupId,
						 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						 Poco::JSON::Array::Ptr &ArrayResponse,
						 Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/devices";
		return ExecuteGetArray(client, endpoint, CallStatus, ArrayResponse, ObjectResponse);
	}

	bool CreateGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId, const Poco::JSON::Object &Body,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/devices";
		return ExecutePostObject(client, endpoint, Body, 120000, CallStatus, CallResponse);
	}

	bool GetGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &GroupId, const std::string &ClientMac,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/devices/" + ClientMac;
		return ExecuteGetObject(client, endpoint, CallStatus, CallResponse);
	}

	bool DeleteGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId, const std::string &ClientMac,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/devices/" + ClientMac;
		if (!ExecuteDelete(client, endpoint, 120000, CallStatus, CallResponse, RawResponseBody)) {
			return false;
		}
		if (RawResponseBody.empty() || !CallResponse || !CallResponse->has("config-raw")) {
			return false;
		}
		return true;
	}

	// =========================================================================
	// Group Schedules
	// =========================================================================

	bool GetGroupSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Array::Ptr &ArrayResponse,
						   Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/schedules";
		return ExecuteGetArray(client, endpoint, CallStatus, ArrayResponse, ObjectResponse);
	}

	bool CreateGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
							 const std::string &GroupId, const Poco::JSON::Object &Body,
							 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/schedules";
		return ExecutePostObject(client, endpoint, Body, 120000, CallStatus, CallResponse);
	}

	bool GetGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						  const std::string &GroupId, const std::string &ScheduleId,
						  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						  Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/schedules/" + ScheduleId;
		return ExecuteGetObject(client, endpoint, CallStatus, CallResponse);
	}

	bool DeleteGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
							 const std::string &GroupId, const std::string &ScheduleId,
							 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							 Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/schedules/" + ScheduleId;
		if (!ExecuteDelete(client, endpoint, 120000, CallStatus, CallResponse, RawResponseBody)) {
			return false;
		}
		if (RawResponseBody.empty() || !CallResponse || !CallResponse->has("config-raw")) {
			return false;
		}
		return true;
	}

	bool ReplaceGroupSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
							   const std::string &GroupId, const Poco::JSON::Object &Body,
							   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							   Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId + "/schedules";
		return ExecutePutObject(client, endpoint, Body, 120000, CallStatus, CallResponse);
	}

} // namespace OpenWifi::SDK::ParentalControl
