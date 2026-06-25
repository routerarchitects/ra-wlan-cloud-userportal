/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include <algorithm>
#include <cctype>
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
#include "RESTAPI/RESTAPI_group_devices_handler.h"
#include "RESTAPI/RESTAPI_group_devices_list_handler.h"
#include "RESTAPI/RESTAPI_parental_control_utils.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "sdks/SDK_parental_control.h"

namespace {

const std::string kValidGroupId = "11111111-1111-4111-8111-111111111111";
const std::string kInvalidGroupId = "bad-group-id";
const std::string kValidMac = "AA:BB:CC:DD:EE:FF";
const std::string kInvalidMac = "invalid-mac";

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
    std::ostringstream os;
    for (std::size_t i = 0; i < value.size(); i += 2) {
        if (i != 0) {
            os << ':';
        }
        os << value.substr(i, 2);
    }
    return os.str();
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

struct DeviceHandlerState {
    bool getListOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getListStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Array::Ptr getListArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr getListError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool createOk = true;
    Poco::Net::HTTPResponse::HTTPStatus createStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr createResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool getSingleOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getSingleStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr getSingleResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool deleteOk = true;
    Poco::Net::HTTPResponse::HTTPStatus deleteStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr deleteResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string deleteRawBody = "{\"config-raw\":[]}";

    OpenWifi::RESTAPI::ParentalControl::ValidateMacResult validateResult =
        OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::Success;
    std::string validateGatewaySerial = "GW-123";
    bool extractConfigRawOk = true;
    Poco::JSON::Array::Ptr extractedConfigRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult applyResult =
        OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult::Applied;

    int validateCalls = 0;
    int createCalls = 0;
    int deleteCalls = 0;
    std::string lastSubscriberId;
    std::string lastGroupId;
    std::string lastClientMac;
    std::string lastGatewaySerial;
};

DeviceHandlerState g_state;

void ResetState() {
    g_state = DeviceHandlerState{};
    g_state.getListArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    g_state.getListError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.createResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
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

class TestGroupDevicesListHandler final : public OpenWifi::RESTAPI_group_devices_list_handler {
  public:
    using OpenWifi::RESTAPI_group_devices_list_handler::RESTAPI_group_devices_list_handler;

    void setParsedBody(const Poco::JSON::Object::Ptr &body) { ParsedBody_ = body; }
};

class TestGroupDevicesHandler final : public OpenWifi::RESTAPI_group_devices_handler {
  public:
    using OpenWifi::RESTAPI_group_devices_handler::RESTAPI_group_devices_handler;
};

} // namespace

namespace OpenWifi::RESTAPI::ParentalControl {

ValidateMacResult ValidateMacInTopology(RESTAPIHandler &, const std::string &subscriberId, const std::string &,
                                        const std::string &clientMac, std::string &gatewaySerial) {
    ++g_state.validateCalls;
    g_state.lastSubscriberId = subscriberId;
    g_state.lastClientMac = clientMac;
    gatewaySerial = g_state.validateGatewaySerial;
    return g_state.validateResult;
}

bool HandleValidateMacResult(RESTAPIHandler &handler, ValidateMacResult result) {
    if (result == ValidateMacResult::Success) {
        return true;
    }
    if (result == ValidateMacResult::MissingSubscriberOrOperator) {
        handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
        return false;
    }
    handler.BadRequest(RESTAPI::Errors::MissingOrInvalidParameters);
    return false;
}

bool ExtractConfigRawSnapshot(const Poco::JSON::Object::Ptr &, Poco::JSON::Array::Ptr &configRaw, bool) {
    configRaw = g_state.extractedConfigRaw;
    return g_state.extractConfigRawOk;
}

ApplyConfigRawResult ApplyConfigRaw(RESTAPIHandler &, Poco::Logger &, const std::string &, const std::string &,
                                    const std::string &objectId, const Poco::JSON::Array::Ptr &, const std::string &,
                                    const std::string &, const std::string &gatewaySerial) {
    g_state.lastGroupId = objectId;
    g_state.lastGatewaySerial = gatewaySerial;
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

bool GetGroupDevices(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                     Poco::Net::HTTPResponse::HTTPStatus &callStatus, Poco::JSON::Array::Ptr &arrayResponse,
                     Poco::JSON::Object::Ptr &objectResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    callStatus = g_state.getListStatus;
    arrayResponse = g_state.getListArray;
    objectResponse = g_state.getListError;
    return g_state.getListOk;
}

bool CreateGroupDevice(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                       const Poco::JSON::Object &body, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                       Poco::JSON::Object::Ptr &callResponse) {
    ++g_state.createCalls;
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    if (body.has("client_mac")) {
        g_state.lastClientMac = body.getValue<std::string>("client_mac");
    }
    callStatus = g_state.createStatus;
    callResponse = g_state.createResponse;
    return g_state.createOk;
}

bool GetGroupDevice(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                    const std::string &clientMac, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                    Poco::JSON::Object::Ptr &callResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    g_state.lastClientMac = clientMac;
    callStatus = g_state.getSingleStatus;
    callResponse = g_state.getSingleResponse;
    return g_state.getSingleOk;
}

bool DeleteGroupDevice(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                       const std::string &clientMac, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                       Poco::JSON::Object::Ptr &callResponse, std::string &rawResponseBody) {
    ++g_state.deleteCalls;
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    g_state.lastClientMac = clientMac;
    callStatus = g_state.deleteStatus;
    callResponse = g_state.deleteResponse;
    rawResponseBody = g_state.deleteRawBody;
    return g_state.deleteOk;
}

} // namespace OpenWifi::SDK::ParentalControl

#include "../../src/RESTAPI/RESTAPI_group_devices_list_handler.cpp"
#include "../../src/RESTAPI/RESTAPI_group_devices_handler.cpp"

namespace {

void TestListGetRejectsMissingSubscriberId() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/devices", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_FORBIDDEN),
             "missing subscriber should return 403");
}

void TestListGetRejectsInvalidGroupId() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kInvalidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/devices", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "invalid group id should return 400");
}

void TestListGetReturnsJSONArrayOnSuccess() {
    auto device = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    device->set("client_mac", kValidMac);
    g_state.getListArray->add(device);

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_GET, "/api/v1/groups/x/devices", "", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoGet();
    auto array = ParseArray(response.body());
    ExpectEq(array->size(), static_cast<std::size_t>(1), "GET list should return one device");
    ExpectEq(g_state.lastGroupId, kValidGroupId, "group id should be forwarded to SDK");
    ExpectEq(g_state.lastSubscriberId, std::string("subscriber-1"), "subscriber id should be forwarded to SDK");
}

void TestPostRejectsMissingOwner() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("client_mac", kValidMac);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/api/v1/groups/x/devices", "{\"client_mac\":\"AA:BB:CC:DD:EE:FF\"}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPost();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_FORBIDDEN),
             "missing owner should return 403");
}

void TestPostRejectsInvalidClientMac() {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("client_mac", kInvalidMac);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/api/v1/groups/x/devices", "{\"client_mac\":\"invalid\"}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPost();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST),
             "invalid client_mac should return 400");
    ExpectEq(g_state.validateCalls, 0, "topology validation should not run for invalid MAC");
    ExpectEq(g_state.createCalls, 0, "SDK create should not run for invalid MAC");
}

void TestPostStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("client_mac", kValidMac);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesListHandler handler({{"group_id", kValidGroupId}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    body->set("client_mac", kValidMac);
    handler.setParsedBody(body);
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_POST, "/api/v1/groups/x/devices", "{\"client_mac\":\"AA:BB:CC:DD:EE:FF\"}", response);
    handler.Request = &request;
    handler.Response = &response;

    handler.DoPost();
    auto parsed = ParseObject(response.body());
    Expect(!parsed->has("config-raw"), "POST response should strip config-raw");
    ExpectEq(parsed->getValue<std::string>("client_mac"), std::string(kValidMac), "client_mac should remain in response");
    ExpectEq(g_state.lastGatewaySerial, std::string("GW-123"), "gateway serial should be passed to ApplyConfigRaw");
}

void TestDeleteReturnsOkOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.deleteResponse = responseObject;

    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_group_devices_handlers");
    TestGroupDevicesHandler handler({{"group_id", kValidGroupId}, {"client_mac", kValidMac}}, logger, server, 1, false);
    handler.UserInfo_.userinfo.id = "subscriber-1";
    handler.UserInfo_.userinfo.owner = "operator-1";
    FakeResponse response;
    FakeRequest request(Poco::Net::HTTPRequest::HTTP_DELETE, "/api/v1/groups/x/devices/y", "", response);

    handler.Request = &request;
    handler.Response = &response;

    handler.DoDelete();
    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(Poco::Net::HTTPResponse::HTTP_OK),
             "successful delete should return 200");
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"ListGetRejectsMissingSubscriberId", TestListGetRejectsMissingSubscriberId},
    {"ListGetRejectsInvalidGroupId", TestListGetRejectsInvalidGroupId},
    {"ListGetReturnsJSONArrayOnSuccess", TestListGetReturnsJSONArrayOnSuccess},
    {"PostRejectsMissingOwner", TestPostRejectsMissingOwner},
    {"PostRejectsInvalidClientMac", TestPostRejectsInvalidClientMac},
    {"PostStripsConfigRawAndReturnsObject", TestPostStripsConfigRawAndReturnsObject},
    {"DeleteReturnsOkOnSuccess", TestDeleteReturnsOkOnSuccess},
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
