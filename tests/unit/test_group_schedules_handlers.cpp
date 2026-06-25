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
#include "Poco/JSON/Parser.h"
#include "Poco/Logger.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/SocketAddress.h"
#include "RESTAPI/RESTAPI_group_schedules_handler.h"
#include "RESTAPI/RESTAPI_group_schedules_list_handler.h"
#include "RESTAPI/RESTAPI_parental_control_utils.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "sdks/SDK_parental_control.h"

namespace {

const std::string kValidGroupId = "11111111-1111-4111-8111-111111111111";
const std::string kValidScheduleId = "22222222-2222-4222-8222-222222222222";
const std::string kAnotherScheduleId = "33333333-3333-4333-8333-333333333333";
const std::string kInvalidUuid = "not-a-uuid";

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

class FakeHTTPServerParams final : public Poco::Net::HTTPServerParams {
  public:
    ~FakeHTTPServerParams() override = default;
};

class FakeResponse final : public Poco::Net::HTTPServerResponse {
  public:
    void sendContinue() override {}

    std::ostream &send() override {
        sent_ = true;
        return body_;
    }

    void sendFile(const std::string &, const std::string &) override { sent_ = true; }

    void sendBuffer(const void *buffer, std::size_t length) override {
        sent_ = true;
        body_.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(length));
    }

    void redirect(const std::string &uri, HTTPStatus status = HTTP_FOUND) override {
        setStatus(status);
        set("Location", uri);
        sent_ = true;
    }

    void requireAuthentication(const std::string &realm) override {
        setStatus(HTTP_UNAUTHORIZED);
        set("WWW-Authenticate", realm);
        sent_ = true;
    }

    bool sent() const override { return sent_; }
    std::string body() const { return body_.str(); }

  private:
    bool sent_ = false;
    std::ostringstream body_;
};

class FakeRequest final : public Poco::Net::HTTPServerRequest {
  public:
    FakeRequest(const std::string &method, const std::string &uri, const std::string &body, FakeResponse &response)
        : bodyStream_(body), response_(response), clientAddress_("127.0.0.1", 1111), serverAddress_("127.0.0.1", 16006) {
        setMethod(method);
        setURI(uri);
        setVersion(Poco::Net::HTTPMessage::HTTP_1_1);
        if (!body.empty()) {
            setContentType("application/json");
            setContentLength(static_cast<int>(body.size()));
        }
    }

    std::istream &stream() override { return bodyStream_; }
    const Poco::Net::SocketAddress &clientAddress() const override { return clientAddress_; }
    const Poco::Net::SocketAddress &serverAddress() const override { return serverAddress_; }
    const Poco::Net::HTTPServerParams &serverParams() const override { return params_; }
    Poco::Net::HTTPServerResponse &response() const override { return response_; }
    bool secure() const override { return false; }

  private:
    std::istringstream bodyStream_;
    FakeResponse &response_;
    Poco::Net::SocketAddress clientAddress_;
    Poco::Net::SocketAddress serverAddress_;
    FakeHTTPServerParams params_;
};

struct ScheduleHandlerState {
    bool getListOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getListStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Array::Ptr getListArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr getListError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool createOk = true;
    Poco::Net::HTTPResponse::HTTPStatus createStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr createResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool replaceOk = true;
    Poco::Net::HTTPResponse::HTTPStatus replaceStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr replaceResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool getSingleOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getSingleStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr getSingleResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool deleteOk = true;
    Poco::Net::HTTPResponse::HTTPStatus deleteStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr deleteResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string deleteRawBody = "{\"config-raw\":[]}";

    bool extractConfigRawOk = true;
    Poco::JSON::Array::Ptr extractedConfigRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult applyResult =
        OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult::Applied;

    std::string lastSubscriberId;
    std::string lastGroupId;
    std::string lastScheduleId;
    std::size_t replaceCount = 0;
};

ScheduleHandlerState g_state;

void ResetState() {
    g_state = ScheduleHandlerState{};
    g_state.getListArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    g_state.getListError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.createResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.replaceResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.getSingleResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.deleteResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.extractedConfigRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
}

Poco::JSON::Object::Ptr ParseObject(const std::string &body) {
    Poco::JSON::Parser parser;
    return parser.parse(body).extract<Poco::JSON::Object::Ptr>();
}

Poco::JSON::Array::Ptr ParseArray(const std::string &body) {
    Poco::JSON::Parser parser;
    return parser.parse(body).extract<Poco::JSON::Array::Ptr>();
}

class TestGroupSchedulesListHandler final : public OpenWifi::RESTAPI_group_schedules_list_handler {
  public:
    using OpenWifi::RESTAPI_group_schedules_list_handler::RESTAPI_group_schedules_list_handler;

    void setParsedBody(const Poco::JSON::Object::Ptr &body) { ParsedBody_ = body; }
};

class TestGroupSchedulesHandler final : public OpenWifi::RESTAPI_group_schedules_handler {
  public:
    using OpenWifi::RESTAPI_group_schedules_handler::RESTAPI_group_schedules_handler;
};

} // namespace


namespace OpenWifi::RESTAPI::ParentalControl {

bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &, Poco::JSON::Array::Ptr &configRaw, bool) {
    configRaw = g_state.extractedConfigRaw;
    return g_state.extractConfigRawOk;
}

ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &, Poco::Logger &, const std::string &subscriberId, const std::string &,
                                    const std::string &objectId, const Poco::JSON::Array::Ptr &, const std::string &,
                                    const std::string &, const std::string &) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = objectId;
    return g_state.applyResult;
}

bool HandleApplyConfigRawResult(RESTAPIHandler &handler, ApplyConfigRawResult result) {
    if (result == ApplyConfigRawResult::Applied || result == ApplyConfigRawResult::NoConfigApplyNeeded) {
        return true;
    }
    if (result == ApplyConfigRawResult::MissingOperatorId) {
        handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
        return false;
    }
    handler.InternalError(RESTAPI::Errors::InternalError);
    return false;
}

} // namespace OpenWifi::RESTAPI::ParentalControl

namespace OpenWifi::SDK::ParentalControl {

bool GetGroupSchedules(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                       Poco::Net::HTTPResponse::HTTPStatus &callStatus, Poco::JSON::Array::Ptr &arrayResponse,
                       Poco::JSON::Object::Ptr &objectResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    callStatus = g_state.getListStatus;
    arrayResponse = g_state.getListArray;
    objectResponse = g_state.getListError;
    return g_state.getListOk;
}

bool CreateGroupSchedule(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                         const Poco::JSON::Object &body, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                         Poco::JSON::Object::Ptr &callResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    if (body.has("schedule_id")) {
        g_state.lastScheduleId = body.getValue<std::string>("schedule_id");
    }
    callStatus = g_state.createStatus;
    callResponse = g_state.createResponse;
    return g_state.createOk;
}

bool ReplaceGroupSchedules(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                           const Poco::JSON::Object &body, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                           Poco::JSON::Object::Ptr &callResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    if (body.has("schedule_ids") && body.isArray("schedule_ids")) {
        g_state.replaceCount = body.getArray("schedule_ids")->size();
    }
    callStatus = g_state.replaceStatus;
    callResponse = g_state.replaceResponse;
    return g_state.replaceOk;
}

bool GetGroupSchedule(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                      const std::string &scheduleId, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                      Poco::JSON::Object::Ptr &callResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    g_state.lastScheduleId = scheduleId;
    callStatus = g_state.getSingleStatus;
    callResponse = g_state.getSingleResponse;
    return g_state.getSingleOk;
}

bool DeleteGroupSchedule(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                         const std::string &scheduleId, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                         Poco::JSON::Object::Ptr &callResponse, std::string &rawResponseBody) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    g_state.lastScheduleId = scheduleId;
    callStatus = g_state.deleteStatus;
    callResponse = g_state.deleteResponse;
    rawResponseBody = g_state.deleteRawBody;
    return g_state.deleteOk;
}

} // namespace OpenWifi::SDK::ParentalControl

#include "../../src/RESTAPI/RESTAPI_group_schedules_list_handler.cpp"
#include "../../src/RESTAPI/RESTAPI_group_schedules_handler.cpp"

namespace {

void TestListGetRejectsInvalidGroupUuid() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kInvalidUuid}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/schedules", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "invalid group uuid should return 400");
}

void TestListGetReturnsJsonArrayOnSuccess() {
    auto item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("schedule_id", kValidScheduleId);
    g_state.getListArray->add(item);

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/schedules", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    auto array = ParseArray(response.body());
    ExpectEq(array->size(), static_cast<std::size_t>(1), "GET schedules should return one entry");
    ExpectEq(g_state.lastGroupId, kValidGroupId, "group id should be forwarded");
}

void TestPostRejectsInvalidScheduleId() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("schedule_id", kInvalidUuid);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/api/v1/groups/x/schedules", "{\"schedule_id\":\"bad\"}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPost();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "invalid schedule_id should return 400");
}

void TestPostStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("schedule_id", kValidScheduleId);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("schedule_id", kValidScheduleId);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/api/v1/groups/x/schedules", "{\"schedule_id\":\"ok\"}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPost();
    auto parsed = ParseObject(response.body());
    Expect(!parsed->has("config-raw"), "POST response should strip config-raw");
    ExpectEq(parsed->getValue<std::string>("schedule_id"), kValidScheduleId, "schedule_id should remain");
}

void TestPutRejectsDuplicateScheduleIds() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    ids->add(kValidScheduleId);
    ids->add(kValidScheduleId);
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("schedule_ids", ids);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_PUT, "/api/v1/groups/x/schedules", "{\"schedule_ids\":[]}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPut();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "duplicate schedule_ids should return 400");
}

void TestPutRejectsNonStringScheduleIdEntry() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    ids->add(7);
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("schedule_ids", ids);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_PUT, "/api/v1/groups/x/schedules", "{\"schedule_ids\":[]}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPut();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "non-string schedule_ids entry should return 400");
}

void TestPutStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("replaced", true);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.replaceResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    ids->add(kValidScheduleId);
    ids->add(kAnotherScheduleId);
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("schedule_ids", ids);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_PUT, "/api/v1/groups/x/schedules", "{\"schedule_ids\":[]}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPut();
    auto parsed = ParseObject(response.body());
    Expect(!parsed->has("config-raw"), "PUT response should strip config-raw");
    ExpectEq(g_state.replaceCount, static_cast<std::size_t>(2), "two schedule ids should be forwarded");
}

void TestSingleGetReturnsObjectOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("schedule_id", kValidScheduleId);
    g_state.getSingleResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesHandler handler({{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/schedules/y", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    auto parsed = ParseObject(response.body());
    ExpectEq(parsed->getValue<std::string>("schedule_id"), kValidScheduleId, "GET should return schedule object");
}

void TestSingleDeleteRejectsMissingOwner() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesHandler handler({{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_DELETE, "/api/v1/groups/x/schedules/y", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoDelete();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_FORBIDDEN),
             "missing owner should return 403");
}

void TestSingleDeleteReturnsOkOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.deleteResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_schedules_handlers");
    TestGroupSchedulesHandler handler({{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_DELETE, "/api/v1/groups/x/schedules/y", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoDelete();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_OK),
             "successful delete should return 200");
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"ListGetRejectsInvalidGroupUuid", TestListGetRejectsInvalidGroupUuid},
    {"ListGetReturnsJsonArrayOnSuccess", TestListGetReturnsJsonArrayOnSuccess},
    {"PostRejectsInvalidScheduleId", TestPostRejectsInvalidScheduleId},
    {"PostStripsConfigRawAndReturnsObject", TestPostStripsConfigRawAndReturnsObject},
    {"PutRejectsDuplicateScheduleIds", TestPutRejectsDuplicateScheduleIds},
    {"PutRejectsNonStringScheduleIdEntry", TestPutRejectsNonStringScheduleIdEntry},
    {"PutStripsConfigRawAndReturnsObject", TestPutStripsConfigRawAndReturnsObject},
    {"SingleGetReturnsObjectOnSuccess", TestSingleGetReturnsObjectOnSuccess},
    {"SingleDeleteRejectsMissingOwner", TestSingleDeleteRejectsMissingOwner},
    {"SingleDeleteReturnsOkOnSuccess", TestSingleDeleteReturnsOkOnSuccess},
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
    bool ValidUUID(const std::string &uuid) {
        return uuid.size() == 36 && uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' && uuid[23] == '-';
    }
}
