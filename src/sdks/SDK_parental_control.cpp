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
	} // namespace

	bool GetGroups(RESTAPIHandler *client, const std::string &SubscriberId,
	               Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	               Poco::JSON::Array::Ptr &ArrayResponse,
	               Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups";
		auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
		CallStatus = API.Do(ArrayResponse, ObjectResponse,
		                    client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedArraySuccess(CallStatus, ArrayResponse);
	}

	bool CreateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups";
		auto API = OpenAPIRequestPost(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, 60000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool GetGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	              const std::string &GroupId,
	              Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	              Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool UpdateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId, const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		auto API = OpenAPIRequestPut(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, 60000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool DeleteGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse,
	                 std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/groups/" + GroupId;
		auto API = OpenAPIRequestDelete(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
		CallStatus = API.Do(CallResponse, RawResponseBody,
		                    client ? client->UserInfo_.webtoken.access_token_ : "");
		if (CallStatus != Poco::Net::HTTPResponse::HTTP_OK) {
			return false;
		}
		if (RawResponseBody.empty()) {
			return true;
		}
		return CallResponse != nullptr;
	}

	bool GetSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
					  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					  Poco::JSON::Array::Ptr &ArrayResponse,
					  Poco::JSON::Object::Ptr &ObjectResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules";
		auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
		CallStatus = API.Do(ArrayResponse, ObjectResponse,
							client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedArraySuccess(CallStatus, ArrayResponse);
	}

	bool CreateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules";
		auto API = OpenAPIRequestPost(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, 60000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool GetSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
					 const std::string &ScheduleId, Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					 Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		auto API = OpenAPIRequestGet(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 60000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool UpdateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId, const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		auto API = OpenAPIRequestPut(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, Body, 120000);
		CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
		return IsParsedObjectSuccess(CallStatus, CallResponse);
	}

	bool DeleteSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody) {
		std::string endpoint = "/api/v1/subscribers/" + SubscriberId + "/schedules/" + ScheduleId;
		auto API = OpenAPIRequestDelete(uSERVICE_MANGO_PARENTAL_CONTROL, endpoint, {}, 120000);
		CallStatus = API.Do(CallResponse, RawResponseBody,
							client ? client->UserInfo_.webtoken.access_token_ : "");
		if (CallStatus != Poco::Net::HTTPResponse::HTTP_OK) {
			return false;
		}
		if (RawResponseBody.empty()) {
			return true;
		}
		return CallResponse != nullptr;
	}

} // namespace OpenWifi::SDK::ParentalControl
