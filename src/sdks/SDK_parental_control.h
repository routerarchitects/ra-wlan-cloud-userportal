/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include "framework/RESTAPI_Handler.h"
#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include <string>

namespace OpenWifi::SDK::ParentalControl {

	// =========================================================================
	// Groups
	// =========================================================================

	// Success body is a JSON array of Group objects.
	// On HTTP 200: ArrayResponse is populated.
	// On non-200: ObjectResponse is populated (for ForwardErrorResponse passthrough).
	bool GetGroups(RESTAPIHandler *client, const std::string &SubscriberId,
	               Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	               Poco::JSON::Array::Ptr &ArrayResponse,
	               Poco::JSON::Object::Ptr &ObjectResponse);

	bool CreateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse);

	bool GetGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	              const std::string &GroupId,
	              Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	              Poco::JSON::Object::Ptr &CallResponse);

	bool UpdateGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId, const Poco::JSON::Object &Body,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse);

	bool DeleteGroup(RESTAPIHandler *client, const std::string &SubscriberId,
	                 const std::string &GroupId,
	                 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
	                 Poco::JSON::Object::Ptr &CallResponse,
	                 std::string &RawResponseBody);

	// =========================================================================
	// Schedules
	// =========================================================================

	bool GetSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
					  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					  Poco::JSON::Array::Ptr &ArrayResponse,
					  Poco::JSON::Object::Ptr &ObjectResponse);

	bool CreateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse);

	bool GetSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
					 const std::string &ScheduleId, Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
					 Poco::JSON::Object::Ptr &CallResponse);

	bool UpdateSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId, const Poco::JSON::Object &Body,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse);

	bool DeleteSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &ScheduleId,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody);

	// =========================================================================
	// Group Devices
	// =========================================================================

	bool GetGroupDevices(RESTAPIHandler *client, const std::string &SubscriberId,
						 const std::string &GroupId,
						 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						 Poco::JSON::Array::Ptr &ArrayResponse,
						 Poco::JSON::Object::Ptr &ObjectResponse);

	bool CreateGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId, const Poco::JSON::Object &Body,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse);

	bool GetGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						const std::string &GroupId, const std::string &ClientMac,
						Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse);

	bool DeleteGroupDevice(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId, const std::string &ClientMac,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody);

	// =========================================================================
	// Group Schedules
	// =========================================================================

	bool GetGroupSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
						   const std::string &GroupId,
						   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Array::Ptr &ArrayResponse,
						   Poco::JSON::Object::Ptr &ObjectResponse);

	bool CreateGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
							 const std::string &GroupId, const Poco::JSON::Object &Body,
							 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							 Poco::JSON::Object::Ptr &CallResponse);

	bool GetGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
						  const std::string &GroupId, const std::string &ScheduleId,
						  Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
						  Poco::JSON::Object::Ptr &CallResponse);

	bool DeleteGroupSchedule(RESTAPIHandler *client, const std::string &SubscriberId,
							 const std::string &GroupId, const std::string &ScheduleId,
							 Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							 Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody);

	bool ReplaceGroupSchedules(RESTAPIHandler *client, const std::string &SubscriberId,
							   const std::string &GroupId, const Poco::JSON::Object &Body,
							   Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							   Poco::JSON::Object::Ptr &CallResponse);

	// =========================================================================
	// Client Access
	// =========================================================================

	bool CreateClientAccess(RESTAPIHandler *client, const std::string &SubscriberId,
							const Poco::JSON::Object &Body,
							Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							Poco::JSON::Object::Ptr &CallResponse);

	bool DeleteClientAccess(RESTAPIHandler *client, const std::string &SubscriberId,
							const std::string &ClientMac,
							Poco::Net::HTTPResponse::HTTPStatus &CallStatus,
							Poco::JSON::Object::Ptr &CallResponse, std::string &RawResponseBody);

} // namespace OpenWifi::SDK::ParentalControl
