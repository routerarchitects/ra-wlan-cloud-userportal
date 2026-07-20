/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Stringifier.h"
#include "Poco/Logger.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "sdks/SDK_parental_control.h"
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

    std::string lastMethod;
    std::string lastType;
    std::string lastEndpoint;
    std::string lastBearerToken;
    std::string lastBodyJson;
};

RequestStubState g_state;

void ResetState() {
    g_state = RequestStubState{};
    g_state.nextObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.nextArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
}

} // namespace


namespace OpenWifi {

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestGet::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "GET_OBJECT";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    responseObject = g_state.nextObject;
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
    responseObject = g_state.nextObject;
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestPost::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "POST";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    g_state.lastBodyJson = ToJSONString(Body_);
    responseObject = g_state.nextObject;
    return g_state.nextStatus;
}

Poco::Net::HTTPServerResponse::HTTPStatus
OpenAPIRequestPut::Do(Poco::JSON::Object::Ptr &responseObject, const std::string &bearerToken) {
    g_state.lastMethod = "PUT";
    g_state.lastType = Type_;
    g_state.lastEndpoint = EndPoint_;
    g_state.lastBearerToken = bearerToken;
    g_state.lastBodyJson = ToJSONString(Body_);
    responseObject = g_state.nextObject;
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
    responseObject = g_state.nextObject;
    rawResponseBody = g_state.nextRawBody;
    return g_state.nextStatus;
}

} // namespace OpenWifi

#include "../../src/sdks/SDK_parental_control.cpp"

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

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"GetGroupDevicesSuccess", TestGetGroupDevicesSuccess},
    {"CreateGroupDeviceSuccess", TestCreateGroupDeviceSuccess},
    {"GetGroupDeviceSuccess", TestGetGroupDeviceSuccess},
    {"DeleteGroupDeviceSuccessRequiresRawBodyAndConfigRaw", TestDeleteGroupDeviceSuccessRequiresRawBodyAndConfigRaw},
    {"DeleteGroupDeviceFailsWhenRawBodyIsEmpty", TestDeleteGroupDeviceFailsWhenRawBodyIsEmpty},
    {"GetGroupSchedulesSuccess", TestGetGroupSchedulesSuccess},
    {"CreateGroupScheduleSuccess", TestCreateGroupScheduleSuccess},
    {"GetGroupScheduleSuccess", TestGetGroupScheduleSuccess},
    {"DeleteGroupScheduleSuccessRequiresConfigRaw", TestDeleteGroupScheduleSuccessRequiresConfigRaw},
    {"DeleteGroupScheduleFailsWhenConfigRawIsMissing", TestDeleteGroupScheduleFailsWhenConfigRawIsMissing},
    {"ReplaceGroupSchedulesSuccess", TestReplaceGroupSchedulesSuccess},
    {"Non200StatusFailsWrapper", TestNon200StatusFailsWrapper},
    {"BearerTokenIsNotForwardedFromClient", TestBearerTokenIsNotForwardedFromClient},
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
}
