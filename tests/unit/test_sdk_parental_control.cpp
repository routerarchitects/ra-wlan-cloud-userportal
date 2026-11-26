/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Poco/DateTimeParser.h"
#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Parser.h"
#include "Poco/JSON/Stringifier.h"
#include "Poco/Logger.h"
#include "Poco/Timespan.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "sdks/SDK_parental_control.h"
#include "sdks/SDK_gw.h"
#include "framework/OpenAPIRequests.h"

namespace {

class TestFailure : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

void Expect(bool condition, const std::string &message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T, typename U> void ExpectEq(const T &actual, const U &expected, const std::string &message) {
    if (!(actual == expected)) {
        std::ostringstream os;
        os << message << " expected=" << expected << " actual=" << actual;
        throw TestFailure(os.str());
    }
}

std::string ToJSONString(const Poco::JSON::Object &body) {
    std::ostringstream os;
    Poco::JSON::Stringifier::stringify(body, os);
    return os.str();
}

bool IsStrictCanonicalIsoUtc(const std::string &value, Poco::DateTime &outDt) {
    if (value.size() != 19) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i == 4 || i == 7) {
            if (value[i] != '-') return false;
        } else if (i == 10) {
            if (value[i] != 'T') return false;
        } else if (i == 13 || i == 16) {
            if (value[i] != ':') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(value[i]))) {
            return false;
        }
    }
    int tz = 0;
    return Poco::DateTimeParser::tryParse("%Y-%m-%dT%H:%M:%S", value, outDt, tz);
}

void VerifyClientAccessIsoPayload(const std::string &jsonStr,
                                  int64_t expectedDurationMinutes,
                                  const std::string &expectedStartIso = "",
                                  const std::string &expectedStopIso = "") {
    Poco::JSON::Parser parser;
    auto parsed = parser.parse(jsonStr);
    auto obj = parsed.extract<Poco::JSON::Object::Ptr>();
    Expect(obj != nullptr, "Downstream client-access payload must be a JSON object");
    Expect(obj->has("start_date") && obj->has("stop_date"),
           "Timed client-access payload must contain start_date and stop_date");
    Expect(!obj->has("start_time") && !obj->has("stop_time"),
           "Timed client-access payload must omit legacy start_time and stop_time");

    std::string startDate = obj->getValue<std::string>("start_date");
    std::string stopDate = obj->getValue<std::string>("stop_date");

    Poco::DateTime startDt, stopDt;
    Expect(IsStrictCanonicalIsoUtc(startDate, startDt),
           "start_date must strictly match YYYY-MM-DDTHH:MM:SS: " + startDate);
    Expect(IsStrictCanonicalIsoUtc(stopDate, stopDt),
           "stop_date must strictly match YYYY-MM-DDTHH:MM:SS: " + stopDate);

    int64_t diffSeconds = Poco::Timespan(stopDt.timestamp() - startDt.timestamp()).totalSeconds();
    ExpectEq(diffSeconds, expectedDurationMinutes * 60,
             "stop_date - start_date must equal requested duration in seconds");

    if (!expectedStartIso.empty()) {
        ExpectEq(startDate, expectedStartIso, "start_date must match expected deterministic timestamp");
    }
    if (!expectedStopIso.empty()) {
        ExpectEq(stopDate, expectedStopIso, "stop_date must match expected deterministic timestamp");
    }
}

class DummyClient final : public OpenWifi::RESTAPIHandler {
  public:
    DummyClient(Poco::Logger &logger, OpenWifi::RESTAPI_GenericServerAccounting &server)
        : OpenWifi::RESTAPIHandler({}, logger,
                                   {Poco::Net::HTTPRequest::HTTP_GET, Poco::Net::HTTPRequest::HTTP_POST,
                                    Poco::Net::HTTPRequest::HTTP_PUT, Poco::Net::HTTPRequest::HTTP_DELETE},
                                   server, 1, false, false) {}

    void DoGet() override {}
    void DoDelete() override {}
    void DoPost() override {}
    void DoPut() override {}
};

struct RequestStubState {
    Poco::Net::HTTPResponse::HTTPStatus nextStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr nextObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr nextArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    std::string nextRawBody;
    std::deque<Poco::JSON::Object::Ptr> queuedObjects;
    std::deque<std::string> queuedRawBodies;

    std::string lastMethod;
    std::string lastType;
    std::string lastEndpoint;
    std::string lastBearerToken;
    std::string lastBodyJson;
    std::string lastClientAccessEndpoint;
    std::vector<std::string> clientAccessBodiesJson;
    std::vector<std::string> configureBodiesJson;
};

RequestStubState g_state;

void ResetState() {
    g_state = RequestStubState{};
    g_state.nextObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.nextArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
}

Poco::JSON::Object::Ptr PopNextObject() {
    if (!g_state.queuedObjects.empty()) {
        auto obj = g_state.queuedObjects.front();
        g_state.queuedObjects.pop_front();
        return obj;
    }
    return g_state.nextObject;
}

std::string PopNextRawBody() {
    if (!g_state.queuedRawBodies.empty()) {
        auto raw = g_state.queuedRawBodies.front();
        g_state.queuedRawBodies.pop_front();
        return raw;
    }
    return g_state.nextRawBody;
}

} // namespace


namespace OpenWifi {

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestGet::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "GET_OBJECT";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    responseObject = PopNextObject();
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestGet::Do(Poco::JSON::Array::Ptr &responseArray, Poco::JSON::Object::Ptr &responseObject,
                      const std::string &bearerToken) {
    g_state.lastMethod = "GET_ARRAY";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    responseArray = g_state.nextArray;
    responseObject = PopNextObject();
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestPost::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "POST";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    g_state.lastBodyJson = ToJSONString(Body_);
    if (EndPoint_.find("/configure") != std::string::npos) {
        g_state.configureBodiesJson.push_back(g_state.lastBodyJson);
    }
    if (EndPoint_.find("/client-access") != std::string::npos) {
        g_state.lastClientAccessEndpoint = EndPoint_;
        g_state.clientAccessBodiesJson.push_back(g_state.lastBodyJson);
    }
    responseObject = PopNextObject();
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestPut::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "PUT";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    g_state.lastBodyJson = ToJSONString(Body_);
    responseObject = PopNextObject();
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestDelete::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    std::string rawBody;
    return Do(responseObject, rawBody, bearerToken);
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestDelete::Do(Poco::JSON::Object::Ptr &responseObject, std::string &rawResponseBody,
                         const std::string &bearerToken) {
    g_state.lastMethod = "DELETE";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    responseObject = PopNextObject();
    rawResponseBody = PopNextRawBody();
    return g_state.nextStatus;
}

} // namespace OpenWifi

#include "../../src/sdks/SDK_parental_control.cpp"
#include "../../src/sdks/SDK_gw.cpp"

namespace {

void TestGetGroupDevicesSuccess() {
    auto arrayResponse = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arrayResponse->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    g_state.nextArray = arrayResponse;
    g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Array::Ptr actualArray;
    Poco::JSON::Object::Ptr actualObject;
    Expect(OpenWifi::SDK::ParentalControl::GetGroupDevices(nullptr, "sub-1", "group-1", callStatus, actualArray, actualObject),
           "GetGroupDevices should succeed on HTTP 200 with array");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/devices"), "group devices endpoint");
    ExpectEq(g_state.lastMethod, std::string("GET_ARRAY"), "GET array method");
}

void TestCreateGroupDeviceSuccess() {
    Poco::JSON::Object body;
    body.set("client_mac", "AA:BB:CC:DD:EE:FF");

    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("id", "gd-1");
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::CreateGroupDevice(nullptr, "sub-1", "group-1", body, callStatus, actualResponse),
           "CreateGroupDevice should succeed on HTTP 200 with object");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/devices"), "create group device endpoint");
    ExpectEq(g_state.lastMethod, std::string("POST"), "POST method should be used");
    Expect(g_state.lastBodyJson.find("client_mac") != std::string::npos, "POST body should include client_mac");
}

void TestCreateGroupDevicesV2Success() {
    Poco::JSON::Object body;
    auto macs = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    macs->add("AA:BB:CC:DD:EE:01");
    macs->add("AA:BB:CC:DD:EE:02");
    body.set("client_macs", macs);

    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("devices", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::CreateGroupDevicesV2(nullptr, "sub-1", "group-1", body, callStatus, actualResponse),
           "CreateGroupDevicesV2 should succeed on HTTP 200 with object");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v2/subscribers/sub-1/groups/group-1/devices"), "create group devices V2 endpoint");
    ExpectEq(g_state.lastMethod, std::string("POST"), "POST method should be used");
    Expect(g_state.lastBodyJson.find("client_macs") != std::string::npos, "POST body should include client_macs");
}

void TestGetGroupDeviceSuccess() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("client_mac", "AA:BB:CC:DD:EE:FF");
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::GetGroupDevice(nullptr, "sub-1", "group-1", "AA:BB", callStatus, actualResponse),
           "GetGroupDevice should succeed on HTTP 200 with object");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/devices/AA:BB"), "single group device endpoint");
    ExpectEq(g_state.lastMethod, std::string("GET_OBJECT"), "GET object method should be used");
}

void TestDeleteGroupDeviceSuccessRequiresRawBodyAndConfigRaw() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.nextObject = response;
    g_state.nextRawBody = "{\"config-raw\":[]}";
    g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    std::string rawResponseBody;
    Expect(OpenWifi::SDK::ParentalControl::DeleteGroupDevice(nullptr, "sub-1", "group-1", "AA", callStatus, actualResponse, rawResponseBody),
           "DeleteGroupDevice should require raw body and config-raw");
    ExpectEq(rawResponseBody, std::string("{\"config-raw\":[]}"), "raw delete body should be passed through");
}

void TestDeleteGroupDeviceFailsWhenRawBodyIsEmpty() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.nextObject = response;
    g_state.nextRawBody.clear();
    g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    std::string rawResponseBody;
    Expect(!OpenWifi::SDK::ParentalControl::DeleteGroupDevice(nullptr, "sub-1", "group-1", "AA", callStatus, actualResponse, rawResponseBody),
           "DeleteGroupDevice should fail when raw body is empty");
}

void TestGetGroupSchedulesSuccess() {
    auto arrayResponse = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arrayResponse->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    g_state.nextArray = arrayResponse;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Array::Ptr actualArray;
    Poco::JSON::Object::Ptr actualObject;
    Expect(OpenWifi::SDK::ParentalControl::GetGroupSchedules(nullptr, "sub-1", "group-1", callStatus, actualArray, actualObject),
           "GetGroupSchedules should succeed");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/schedules"), "group schedules endpoint");
}

void TestGetScheduleGroupsSuccess() {
    auto arrayResponse = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto group = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    group->set("id", "11111111-1111-4111-8111-111111111111");
    group->set("name", "Family");
    group->set("device_count", 2);
    arrayResponse->add(group);
    g_state.nextArray = arrayResponse;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Array::Ptr actualArray;
    Poco::JSON::Object::Ptr actualObject;

    Expect(OpenWifi::SDK::ParentalControl::GetScheduleGroups(
               nullptr, "sub-123", "sch-456", callStatus, actualArray, actualObject),
           "GetScheduleGroups should succeed on HTTP 200");
    ExpectEq(g_state.lastMethod, std::string("GET_ARRAY"), "GET_ARRAY method required for array Do call");
    ExpectEq(g_state.lastEndpoint,
             std::string("/api/v1/subscribers/sub-123/schedules/sch-456/groups"),
             "Target endpoint must match Mango ListScheduleGroups route");
    ExpectEq(actualArray->size(), static_cast<std::size_t>(1), "Returned array matches mock");
}

void TestGetScheduleGroupsNotFound() {
    auto errorObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto err = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    err->set("code", "schedule_not_found");
    err->set("message", "Schedule not found");
    errorObject->set("error", err);
    g_state.nextObject = errorObject;
    g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_NOT_FOUND;

    Poco::Net::HTTPResponse::HTTPStatus callStatus;
    Poco::JSON::Array::Ptr actualArray;
    Poco::JSON::Object::Ptr actualObject;

    Expect(!OpenWifi::SDK::ParentalControl::GetScheduleGroups(
               nullptr, "sub-123", "sch-456", callStatus, actualArray, actualObject),
           "GetScheduleGroups should return false on HTTP 404");
    ExpectEq(static_cast<int>(callStatus), 404, "Status should be 404");
    Expect(actualObject != nullptr, "Error object populated on failure");
}

void TestCreateGroupScheduleSuccess() {
    Poco::JSON::Object body;
    body.set("schedule_id", "schedule-1");

    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("schedule_id", "schedule-1");
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::CreateGroupSchedule(nullptr, "sub-1", "group-1", body, callStatus, actualResponse),
           "CreateGroupSchedule should succeed");
    ExpectEq(g_state.lastMethod, std::string("POST"), "POST should be used for CreateGroupSchedule");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/schedules"), "create group schedule endpoint");
}

void TestGetGroupScheduleSuccess() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("schedule_id", "schedule-1");
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::GetGroupSchedule(nullptr, "sub-1", "group-1", "schedule-1", callStatus, actualResponse),
           "GetGroupSchedule should succeed");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/schedules/schedule-1"), "single group schedule endpoint");
}

void TestDeleteGroupScheduleSuccessRequiresConfigRaw() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.nextObject = response;
    g_state.nextRawBody = "{\"config-raw\":[]}";

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    std::string rawResponseBody;
    Expect(OpenWifi::SDK::ParentalControl::DeleteGroupSchedule(nullptr, "sub-1", "group-1", "schedule-1", callStatus, actualResponse, rawResponseBody),
           "DeleteGroupSchedule should require config-raw");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/schedules/schedule-1"), "delete group schedule endpoint");
}

void TestDeleteGroupScheduleFailsWhenConfigRawIsMissing() {
    g_state.nextObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.nextRawBody = "{\"ok\":true}";

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    std::string rawResponseBody;
    Expect(!OpenWifi::SDK::ParentalControl::DeleteGroupSchedule(nullptr, "sub-1", "group-1", "schedule-1", callStatus, actualResponse, rawResponseBody),
           "DeleteGroupSchedule should fail when config-raw is absent");
}

void TestReplaceGroupSchedulesSuccess() {
    Poco::JSON::Object body;
    auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    ids->add("schedule-1");
    body.set("schedule_ids", ids);

    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("count", 1);
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::ReplaceGroupSchedules(nullptr, "sub-1", "group-1", body, callStatus, actualResponse),
           "ReplaceGroupSchedules should succeed");
    ExpectEq(g_state.lastMethod, std::string("PUT"), "PUT should be used for ReplaceGroupSchedules");
    ExpectEq(g_state.lastEndpoint, std::string("/api/v1/subscribers/sub-1/groups/group-1/schedules"), "replace group schedules endpoint");
}

void TestNon200StatusFailsWrapper() {
    g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_BAD_REQUEST;
    g_state.nextObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(!OpenWifi::SDK::ParentalControl::GetGroupDevice(nullptr, "sub-1", "group-1", "AA", callStatus, actualResponse),
           "non-200 object response should fail wrapper");
}

void TestBearerTokenIsNotForwardedFromClient() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_sdk_parental_control");
    DummyClient client(logger, server);
    client.UserInfo_.webtoken.access_token_ = "token-123";

    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("id", "device-1");
    g_state.nextObject = response;

    Poco::Net::HTTPResponse::HTTPStatus callStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    Poco::JSON::Object::Ptr actualResponse;
    Expect(OpenWifi::SDK::ParentalControl::GetGroupDevice(&client, "sub-1", "group-1", "AA", callStatus, actualResponse),
           "GetGroupDevice should still succeed");
    ExpectEq(g_state.lastBearerToken, std::string(""), "bearer token should not be forwarded");
}

void TestSetConfigDurationValidation() {
    // 1. Short same-day duration (2 mins) must succeed and send canonical ISO timestamps with stop - start == 120s
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", 2);
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        Poco::DateTime fixedNow(2026, 11, 25, 14, 0, 0);
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response, fixedNow);
        Expect(success, "SetConfig with 2-min duration should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(1), "client-access POST should be called once");
        VerifyClientAccessIsoPayload(g_state.clientAccessBodiesJson.back(), 2, "2026-11-25T14:00:00", "2026-11-25T14:02:00");
    }

    // 2. Multi-day / overnight duration (1500 mins = 25h <= 43200 max) succeeds with exact canonical timestamps
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", 1500); // 25 hours (within 30-day / 43200 min cap)
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        Poco::DateTime fixedNow(2026, 11, 25, 22, 0, 0);
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response, fixedNow);
        Expect(success, "SetConfig with 1500-min duration should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(1), "client-access POST should be called once");
        VerifyClientAccessIsoPayload(g_state.clientAccessBodiesJson.back(), 1500, "2026-11-25T22:00:00", "2026-11-26T23:00:00");
    }

    // 3. Invalid MAC address must be rejected with HTTP 400 Bad Request (Error 1019)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "invalid-mac-address-here");
        entry->set("access", "deny");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with invalid mac should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1019: Invalid MAC address."),
                 "error text must match InvalidMacAddress");
    }

    // 4. Invalid access mode must be rejected with HTTP 400 Bad Request (Error 1204)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "invalid_mode");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with invalid access should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1204: Access must be 'allow' or 'deny'."),
                 "error text must match InvalidAccess");
    }

    // 5. Duration on 'allow' must be rejected with HTTP 400 Bad Request (Error 1202)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "allow");
        entry->set("duration", 30);
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with duration on allow should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1202: Duration is not allowed when access is set to 'allow'."),
                 "error text must match DurationNotAllowedForAllow");
    }

    // 6. Invalid duration minutes (< 1) must be rejected with HTTP 400 Bad Request (Error 1203)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", 0);
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with invalid duration should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1203: Duration must be a positive integer greater than or equal to 1."),
                 "error text must match InvalidDuration");
     }

    // 7. Access 'allow' with duration: null must be rejected with HTTP 400 (Error 1202)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "allow");
        entry->set("duration", Poco::Dynamic::Var());
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with duration: null on allow should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1202: Duration is not allowed when access is set to 'allow'."),
                 "error text must match DurationNotAllowedForAllow");
    }

    // 8. Exact maximum duration (43200 minutes = 30 days) must succeed with ISO-8601 timestamps
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", 43200);
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        Poco::DateTime fixedNow(2026, 11, 25, 10, 0, 0);
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response, fixedNow);
        Expect(success, "SetConfig with 30-day (43200 min) duration should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(1), "client-access POST should be called once");
        VerifyClientAccessIsoPayload(g_state.clientAccessBodiesJson.back(), 43200, "2026-11-25T10:00:00", "2026-12-25T10:00:00");
    }

    // 9. Duration exceeding maximum (43201 minutes) must be rejected with HTTP 400 Bad Request (Error 1207)
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", 43201);
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with duration > 43200 should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1207: Duration must not exceed 43200 minutes (30 days)."),
                 "error text must match DurationExceedsMaximum");
    }

    // 10. Minimum valid duration (1 minute) must always succeed regardless of current hour
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", static_cast<int64_t>(1));
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(success, "SetConfig with minimum duration=1 should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(1), "client-access POST should be called once");
        VerifyClientAccessIsoPayload(g_state.clientAccessBodiesJson.back(), 1);
    }

    // 12. Deterministic cross-midnight (23:00 -> 01:00) and Dec 31 -> Jan 1 year rollover payload checks
    {
        struct ClockCase {
            std::string label;
            Poco::DateTime nowUtc;
            int64_t duration;
            std::string expectedStart;
            std::string expectedStop;
        };

        const std::vector<ClockCase> cases = {
            {"23:00 -> 01:00 overnight", Poco::DateTime(2026, 11, 25, 23, 0, 0), 120,
             "2026-11-25T23:00:00", "2026-11-26T01:00:00"},
            {"Dec 31 -> Jan 1 year rollover", Poco::DateTime(2026, 12, 31, 23, 30, 0), 120,
             "2026-12-31T23:30:00", "2027-01-01T01:30:00"},
        };

        for (const auto &tc : cases) {
            ResetState();
            auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
            auto configObj = Poco::makeShared<Poco::JSON::Object>();
            deviceObj->set("configuration", configObj);
            deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
            g_state.nextObject = deviceObj;
            g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

            OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
            OpenWifi::ProvObjects::SubscriberDevice dev;
            dev.deviceGroup = "olg";
            dev.serialNumber = "112233445566";
            subDevices.subscriberDevices.push_back(dev);

            auto body = Poco::makeShared<Poco::JSON::Object>();
            auto clientArr = Poco::makeShared<Poco::JSON::Array>();
            auto entry = Poco::makeShared<Poco::JSON::Object>();
            entry->set("mac", "aa:bb:cc:dd:ee:ff");
            entry->set("access", "deny");
            entry->set("duration", tc.duration);
            clientArr->add(entry);
            body->set("client", clientArr);

            Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
            Poco::JSON::Object::Ptr response;
            bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response, tc.nowUtc);
            Expect(success, "SetConfig (" + tc.label + ") should succeed");
            ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
            ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(1), "client-access POST should be called once");
            VerifyClientAccessIsoPayload(g_state.clientAccessBodiesJson.back(), tc.duration, tc.expectedStart, tc.expectedStop);
        }
    }

    // 11. Extreme duration (INT64_MAX) must be rejected in Pass 1 without int64 arithmetic overflow or REST calls
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        entry->set("duration", std::numeric_limits<int64_t>::max());
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with INT64_MAX duration should fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1207: Duration must not exceed 43200 minutes (30 days)."),
                 "error text must match DurationExceedsMaximum");
        Expect(g_state.lastEndpoint.find("client-access") == std::string::npos,
               "No downstream client-access calls should be made on INT64_MAX duration");
    }
}

void TestSetConfigTwoPassValidation() {
    // Case 1: Send multiple clients where the last client has an invalid MAC.
    // Assert response is 400, and assert no downstream parental-control client-access calls are made.
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        
        // Client 1: Valid MAC
        auto entry1 = Poco::makeShared<Poco::JSON::Object>();
        entry1->set("mac", "11:22:33:44:55:66");
        entry1->set("access", "deny");
        clientArr->add(entry1);

        // Client 2: Invalid MAC
        auto entry2 = Poco::makeShared<Poco::JSON::Object>();
        entry2->set("mac", "invalid-mac-address");
        entry2->set("access", "deny");
        clientArr->add(entry2);

        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        
        Expect(!success, "SetConfig should fail because of the invalid MAC in the second client");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1019: Invalid MAC address."),
                 "error text must match InvalidMacAddress");
        
        // Since GetConfig sets the endpoint to /api/v1/device/...
        // If any client-access POST or DELETE call had been made, lastEndpoint would contain "client-access".
        // Assert that lastEndpoint does NOT contain "client-access", verifying no calls were made.
        Expect(g_state.lastEndpoint.find("client-access") == std::string::npos, 
               "No downstream client-access calls should have been made due to early validation failure");
    }

    // Case 1b: Mixed valid + over-max duration (43201) batch must fail in Pass 1 with zero client-access calls.
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();

        auto entry1 = Poco::makeShared<Poco::JSON::Object>();
        entry1->set("mac", "11:22:33:44:55:66");
        entry1->set("access", "deny");
        entry1->set("duration", 60);
        clientArr->add(entry1);

        auto entry2 = Poco::makeShared<Poco::JSON::Object>();
        entry2->set("mac", "aa:bb:cc:dd:ee:ff");
        entry2->set("access", "deny");
        entry2->set("duration", 43201);
        clientArr->add(entry2);

        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "Mixed valid/over-max batch must fail in Pass 1");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, "status should be 400");
        Expect(response && response->has("ErrorDescription"), "response must contain ErrorDescription");
        ExpectEq(response->getValue<std::string>("ErrorDescription"),
                 std::string("1207: Duration must not exceed 43200 minutes (30 days)."),
                 "error text must match DurationExceedsMaximum");
        Expect(g_state.lastEndpoint.find("client-access") == std::string::npos,
               "No downstream client-access calls should be made when any batch entry exceeds max duration");
    }

    // Case 2: Success case confirming valid multi-client input still executes calls normally.
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        deviceObj->set("configuration", configObj);
        deviceObj->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.nextObject = deviceObj;
        g_state.nextStatus = Poco::Net::HTTPResponse::HTTP_OK;

        OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
        OpenWifi::ProvObjects::SubscriberDevice dev;
        dev.deviceGroup = "olg";
        dev.serialNumber = "112233445566";
        subDevices.subscriberDevices.push_back(dev);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        
        // Client 1: Valid MAC
        auto entry1 = Poco::makeShared<Poco::JSON::Object>();
        entry1->set("mac", "11:22:33:44:55:66");
        entry1->set("access", "deny");
        clientArr->add(entry1);

        // Client 2: Valid MAC
        auto entry2 = Poco::makeShared<Poco::JSON::Object>();
        entry2->set("mac", "aa:bb:cc:dd:ee:ff");
        entry2->set("access", "deny");
        clientArr->add(entry2);

        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        
        Expect(success, "SetConfig with multiple valid clients should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        
        // Verify that downstream client-access calls were actually executed.
        ExpectEq(g_state.clientAccessBodiesJson.size(), static_cast<std::size_t>(2),
                 "Two downstream client-access POST calls should have been executed");
        Expect(g_state.lastClientAccessEndpoint.find("client-access") != std::string::npos,
               "Downstream client-access calls should have been executed");
        ExpectEq(g_state.lastMethod, std::string("POST"), "Last call should have been a POST");
    }
}

void TestSetConfigConfigRawHandling() {
    auto makeDeviceWithExistingPolicy = [](const std::string &existingRuleValue) {
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        auto unitObj = Poco::makeShared<Poco::JSON::Object>();
        unitObj->set("name", "test-gateway");
        configObj->set("unit", unitObj);
        auto existingRaw = Poco::makeShared<Poco::JSON::Array>();
        auto cmd = Poco::makeShared<Poco::JSON::Array>();
        cmd->add("set");
        cmd->add("firewall.pc_rule_existing");
        cmd->add(existingRuleValue);
        existingRaw->add(cmd);
        configObj->set("config-raw", existingRaw);
        deviceObj->set("configuration", configObj);
        return deviceObj;
    };

    OpenWifi::ProvObjects::SubscriberDeviceList subDevices;
    OpenWifi::ProvObjects::SubscriberDevice dev;
    dev.deviceGroup = "olg";
    dev.serialNumber = "112233445566";
    subDevices.subscriberDevices.push_back(dev);

    // 1. config-raw: null no-op response must preserve existing config-raw and skip Configure()
    {
        ResetState();
        g_state.queuedObjects.push_back(makeDeviceWithExistingPolicy("rule"));

        auto allowResp = Poco::makeShared<Poco::JSON::Object>();
        allowResp->set("config-raw", Poco::Dynamic::Var());
        g_state.queuedObjects.push_back(allowResp);
        g_state.queuedRawBodies.push_back("{\"config-raw\":null}");

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "allow");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(success, "SetConfig with no-op config-raw: null should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.configureBodiesJson.size(), static_cast<std::size_t>(0), "Configure POST must be skipped on no-op");
    }

    // 2. config-raw: [] response must replace existing rules with explicit empty array [] on gateway
    {
        ResetState();
        g_state.queuedObjects.push_back(makeDeviceWithExistingPolicy("rule"));

        auto allowResp = Poco::makeShared<Poco::JSON::Object>();
        allowResp->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.queuedObjects.push_back(allowResp);
        g_state.queuedRawBodies.push_back("{\"config-raw\":[]}");
        g_state.queuedObjects.push_back(Poco::makeShared<Poco::JSON::Object>()); // Configure POST response

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "allow");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(success, "SetConfig with empty config-raw: [] should succeed");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.configureBodiesJson.size(), static_cast<std::size_t>(1), "Configure POST should be called once");

        Poco::JSON::Parser parser;
        auto sentBody = parser.parse(g_state.configureBodiesJson[0]).extract<Poco::JSON::Object::Ptr>();
        Expect(sentBody && sentBody->has("configuration") && sentBody->isObject("configuration"),
               "Configure payload must contain configuration object");
        auto sentConfig = sentBody->getObject("configuration");
        Expect(sentConfig && sentConfig->has("unit") && sentConfig->isObject("unit"),
               "Configure payload must preserve unrelated configuration sections");
        Expect(sentConfig->has("config-raw") && !sentConfig->isNull("config-raw") && sentConfig->isArray("config-raw"),
               "Configure payload must explicitly forward non-null config-raw array []");
        auto sentRaw = sentConfig->getArray("config-raw");
        ExpectEq(sentRaw->size(), static_cast<std::size_t>(0), "Forwarded config-raw must be an empty array []");
        Expect(g_state.configureBodiesJson[0].find("pc_rule_existing") == std::string::npos,
               "Previously installed rule must be removed from Configure payload");
    }

    // 2b. config-raw: [] still triggers Configure when gateway config had no config-raw property
    {
        ResetState();
        auto deviceObj = Poco::makeShared<Poco::JSON::Object>();
        auto configObj = Poco::makeShared<Poco::JSON::Object>();
        auto unitObj = Poco::makeShared<Poco::JSON::Object>();
        unitObj->set("name", "test-gateway");
        configObj->set("unit", unitObj);
        deviceObj->set("configuration", configObj);
        g_state.queuedObjects.push_back(deviceObj);

        auto allowResp = Poco::makeShared<Poco::JSON::Object>();
        allowResp->set("config-raw", Poco::makeShared<Poco::JSON::Array>());
        g_state.queuedObjects.push_back(allowResp);
        g_state.queuedRawBodies.push_back("{\"config-raw\":[]}");
        g_state.queuedObjects.push_back(Poco::makeShared<Poco::JSON::Object>()); // Configure POST response

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "allow");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(success, "SetConfig with empty config-raw: [] should succeed when gateway lacked config-raw");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "status should be 200");
        ExpectEq(g_state.configureBodiesJson.size(), static_cast<std::size_t>(1),
                 "Configure POST must still be called to forward explicit []");

        Poco::JSON::Parser parser;
        auto sentBody = parser.parse(g_state.configureBodiesJson[0]).extract<Poco::JSON::Object::Ptr>();
        auto sentConfig = sentBody ? sentBody->getObject("configuration") : nullptr;
        Expect(sentConfig && sentConfig->has("config-raw") && sentConfig->isArray("config-raw"),
               "Configure payload must include explicit config-raw: []");
        ExpectEq(sentConfig->getArray("config-raw")->size(), static_cast<std::size_t>(0),
                 "Forwarded config-raw must be empty array []");
    }

    // 3. Batch where first call returns populated array and second returns null no-op retains first snapshot
    {
        ResetState();
        g_state.queuedObjects.push_back(makeDeviceWithExistingPolicy("old_rule"));

        auto denyResp = Poco::makeShared<Poco::JSON::Object>();
        auto newRaw = Poco::makeShared<Poco::JSON::Array>();
        auto cmd = Poco::makeShared<Poco::JSON::Array>();
        cmd->add("set");
        cmd->add("firewall.pc_client_access_new");
        cmd->add("rule");
        newRaw->add(cmd);
        denyResp->set("config-raw", newRaw);
        g_state.queuedObjects.push_back(denyResp);

        auto allowNoOpResp = Poco::makeShared<Poco::JSON::Object>();
        allowNoOpResp->set("config-raw", Poco::Dynamic::Var());
        g_state.queuedObjects.push_back(allowNoOpResp);
        g_state.queuedRawBodies.push_back("{\"config-raw\":null}");
        g_state.queuedObjects.push_back(Poco::makeShared<Poco::JSON::Object>()); // Configure POST response

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry1 = Poco::makeShared<Poco::JSON::Object>();
        entry1->set("mac", "11:22:33:44:55:66");
        entry1->set("access", "deny");
        clientArr->add(entry1);
        auto entry2 = Poco::makeShared<Poco::JSON::Object>();
        entry2->set("mac", "aa:bb:cc:dd:ee:ff");
        entry2->set("access", "allow");
        clientArr->add(entry2);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(success, "SetConfig batch with changed snapshot followed by no-op should succeed");
        ExpectEq(g_state.configureBodiesJson.size(), static_cast<std::size_t>(1), "Configure POST should be called once");
        Expect(g_state.configureBodiesJson[0].find("pc_client_access_new") != std::string::npos, "Configure payload must retain changed snapshot");
    }

    // 4. Invalid non-array config-raw must fail with HTTP 500 without clearing gateway config
    {
        ResetState();
        g_state.queuedObjects.push_back(makeDeviceWithExistingPolicy("rule"));

        auto invalidResp = Poco::makeShared<Poco::JSON::Object>();
        invalidResp->set("config-raw", "invalid-string");
        g_state.queuedObjects.push_back(invalidResp);

        auto body = Poco::makeShared<Poco::JSON::Object>();
        auto clientArr = Poco::makeShared<Poco::JSON::Array>();
        auto entry = Poco::makeShared<Poco::JSON::Object>();
        entry->set("mac", "aa:bb:cc:dd:ee:ff");
        entry->set("access", "deny");
        clientArr->add(entry);
        body->set("client", clientArr);

        Poco::Net::HTTPResponse::HTTPStatus status = Poco::Net::HTTPResponse::HTTP_OK;
        Poco::JSON::Object::Ptr response;
        bool success = OpenWifi::SDK::GW::Device::SetConfig(nullptr, body, subDevices, "112233445566", "sub-123", status, response);
        Expect(!success, "SetConfig with malformed config-raw must fail");
        ExpectEq(status, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "status should be 500");
        ExpectEq(g_state.configureBodiesJson.size(), static_cast<std::size_t>(0), "Configure POST must not be called on malformed response");
    }
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"GetGroupDevicesSuccess", TestGetGroupDevicesSuccess},
    {"CreateGroupDeviceSuccess", TestCreateGroupDeviceSuccess},
    {"CreateGroupDevicesV2Success", TestCreateGroupDevicesV2Success},
    {"GetGroupDeviceSuccess", TestGetGroupDeviceSuccess},
    {"DeleteGroupDeviceSuccessRequiresRawBodyAndConfigRaw", TestDeleteGroupDeviceSuccessRequiresRawBodyAndConfigRaw},
    {"DeleteGroupDeviceFailsWhenRawBodyIsEmpty", TestDeleteGroupDeviceFailsWhenRawBodyIsEmpty},
    {"GetGroupSchedulesSuccess", TestGetGroupSchedulesSuccess},
    {"GetScheduleGroupsSuccess", TestGetScheduleGroupsSuccess},
    {"GetScheduleGroupsNotFound", TestGetScheduleGroupsNotFound},
    {"CreateGroupScheduleSuccess", TestCreateGroupScheduleSuccess},
    {"GetGroupScheduleSuccess", TestGetGroupScheduleSuccess},
    {"DeleteGroupScheduleSuccessRequiresConfigRaw", TestDeleteGroupScheduleSuccessRequiresConfigRaw},
    {"DeleteGroupScheduleFailsWhenConfigRawIsMissing", TestDeleteGroupScheduleFailsWhenConfigRawIsMissing},
    {"ReplaceGroupSchedulesSuccess", TestReplaceGroupSchedulesSuccess},
    {"Non200StatusFailsWrapper", TestNon200StatusFailsWrapper},
    {"BearerTokenIsNotForwardedFromClient", TestBearerTokenIsNotForwardedFromClient},
    {"SetConfigDurationValidation", TestSetConfigDurationValidation},
    {"SetConfigTwoPassValidation", TestSetConfigTwoPassValidation},
    {"SetConfigConfigRawHandling", TestSetConfigConfigRawHandling},
};


} // namespace

int main() {
    int failures = 0;
    for (const auto &test : kTests) {
        try {
            ResetState();
            test.second();
            std::cout << "[PASS] " << test.first << std::endl;
        } catch (const std::exception &e) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << e.what() << std::endl;
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed." << std::endl;
        return 1;
    }

    std::cout << kTests.size() << " test(s) passed." << std::endl;
    return 0;
}

namespace Poco::Util { class Application; }
namespace Poco::Net { class HTTPRequestHandler; }
namespace OpenWifi {
    class RESTAPI_GenericServerAccounting;
    void DaemonPostInitialization(Poco::Util::Application &) {}
    Poco::Net::HTTPRequestHandler* RESTAPI_ExtRouter(const std::string &, std::map<std::string, std::string> &, Poco::Logger &, RESTAPI_GenericServerAccounting &, unsigned long) {
        return nullptr;
    }
    Poco::Net::HTTPRequestHandler* RESTAPI_IntRouter(const std::string &, std::map<std::string, std::string> &, Poco::Logger &, RESTAPI_GenericServerAccounting &, unsigned long) {
        return nullptr;
    }

    SubSystemServer::SubSystemServer(const std::string &Name, const std::string &LoggingPrefix,
                                     const std::string &SubSystemConfigPrefix)
        : Name_(Name), LoggerPrefix_(LoggingPrefix), SubSystemConfigPrefix_(SubSystemConfigPrefix),
          Logger_(std::make_unique<LoggerWrapper>(Poco::Logger::get(LoggingPrefix))) {}

    void SubSystemServer::initialize(Poco::Util::Application &) {}

    bool AllowExternalMicroServices() { return false; }
    bool MicroServiceIsValidAPIKEY(const Poco::Net::HTTPServerRequest &) { return false; }
    bool AuthClient::IsValidApiKey(const std::string &, SecurityObjects::UserInfoAndPolicy &, unsigned long, bool &, bool &, bool &) { return false; }
    bool AuthClient::IsAuthorized(const std::string &, SecurityObjects::UserInfoAndPolicy &, unsigned long, bool &, bool &, bool) { return false; }
}

namespace OpenWifi::Utils {
    std::string FormatIPv6(const std::string &addr) { return addr; }

    std::string StripMac(const std::string &value) {
        std::string result;
        for (char c : value) {
            if (c == ':' || c == '-' || c == '.') {
                continue;
            }
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        return result;
    }

    bool IsNormalizedMac(const std::string &value) {
        if (value.size() != 12) {
            return false;
        }
        return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
    }

    std::string MacWithColons(const std::string &value) {
        if (!IsNormalizedMac(value)) {
            return value;
        }
        std::ostringstream os;
        for (std::size_t i = 0; i < value.size(); i += 2) {
            if (i != 0) {
                os << ':';
            }
            os << value.substr(i, 2);
        }
        return os.str();
    }

    bool NormalizeMac(std::string &mac) {
        std::string normalized = StripMac(mac);
        if (!IsNormalizedMac(normalized)) {
            return false;
        }
        mac = normalized;
        return true;
    }

    std::string SerialToMAC(const std::string &serial) { return MacWithColons(StripMac(serial)); }
}

namespace OpenWifi {
    bool ConfigMaker::BuildMeshConfig(const Poco::JSON::Object::Ptr &inputConfig, Poco::JSON::Object::Ptr &meshConfig) {
        meshConfig = inputConfig;
        return true;
    }
}

namespace OpenWifi::RESTAPI::ParentalControl {
    bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &response, Poco::JSON::Array::Ptr &configRaw, bool required) {
        configRaw.reset();
        if (!response || !response->has("config-raw")) {
            return !required;
        }
        if (response->isNull("config-raw")) {
            return true;
        }
        if (!response->isArray("config-raw")) {
            return false;
        }
        configRaw = response->getArray("config-raw");
        return static_cast<bool>(configRaw);
    }
}
