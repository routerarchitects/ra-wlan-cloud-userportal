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
#include "Poco/Timespan.h"
#include "Poco/Timestamp.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/SocketAddress.h"
#include "RESTAPI/RESTAPI_parental_control_utils.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_nw_topology.h"
#include "sdks/SDK_prov.h"

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

Poco::JSON::Array::Ptr MakeStringArray(std::initializer_list<std::string> values) {
    auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto &value : values) {
        arr->add(value);
    }
    return arr;
}

Poco::JSON::Array::Ptr MakeIntArray(std::initializer_list<int> values) {
    auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (int value : values) {
        arr->add(value);
    }
    return arr;
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

std::string FormatDate(const Poco::DateTime &value) {
    return Poco::DateTimeFormatter::format(value, "%Y-%m-%d");
}

Poco::DateTime ShiftDateTime(const Poco::DateTime &value, int days, int hours = 0, int minutes = 0, int seconds = 0) {
    Poco::Timestamp ts = value.timestamp();
    ts += Poco::Timespan(days, hours, minutes, seconds, 0);
    return Poco::DateTime(ts);
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

class FakeRESTAPIHandler : public OpenWifi::RESTAPIHandler {
  public:
    FakeRESTAPIHandler(Poco::Logger &logger, Poco::Net::HTTPServerRequest *request, Poco::Net::HTTPServerResponse *response)
        : OpenWifi::RESTAPIHandler(
              OpenWifi::RESTAPIHandler::BindingMap{}, 
              logger, 
              std::vector<std::string>{"GET", "POST", "PUT", "DELETE"}, 
              GetServer(), 
              0, 
              false,
              true // AlwaysAuthorize
          ) {
        Request = request;
        Response = response;
    }
    void DoGet() override {}
    void DoDelete() override {}
    void DoPost() override {}
    void DoPut() override {}

  private:
    static OpenWifi::RESTAPI_GenericServerAccounting &GetServer() {
        static OpenWifi::RESTAPI_GenericServerAccounting server;
        return server;
    }
};


struct StubState {
    bool provGetDevicesOk = true;
    Poco::Net::HTTPResponse::HTTPStatus provStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr provResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::vector<std::pair<std::string, std::string>> subscriberDevices;

    bool inventoryOk = true;
    OpenWifi::ProvObjects::InventoryTag inventory;

    bool venueOk = true;
    Poco::Net::HTTPResponse::HTTPStatus venueStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr venueResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    OpenWifi::ProvObjects::Venue venue;

    bool topologyOk = true;
    Poco::Net::HTTPResponse::HTTPStatus topologyStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr topologyResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string lastBoardId;

    bool gatewayGetConfigOk = true;
    Poco::Net::HTTPResponse::HTTPStatus gatewayGetConfigStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr gatewayGetConfigResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

    bool gatewayConfigureOk = true;
    Poco::Net::HTTPResponse::HTTPStatus gatewayConfigureStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr gatewayConfigureResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string configuredSerial;
    Poco::JSON::Object::Ptr configuredObject;
};

StubState g_state;

void ResetStubs() {
    g_state = StubState{};
    g_state.inventory.venue = "venue-1";
    g_state.venue.boards = {"board-1"};

    auto configuration = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    configuration->set("existing", "value");
    auto gatewayObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    gatewayObject->set("configuration", configuration);
    g_state.gatewayGetConfigResponse = gatewayObject;
}

} // namespace


namespace OpenWifi::SDK::Prov::Subscriber {

bool GetDevices(RESTAPIHandler *, const std::string &, const std::string &, ProvObjects::SubscriberDeviceList &devList,
                Poco::Net::HTTPServerResponse::HTTPStatus &callStatus, Poco::JSON::Object::Ptr &callResponse) {
    callStatus = g_state.provStatus;
    callResponse = g_state.provResponse;
    devList.subscriberDevices.clear();
    for (const auto &entry : g_state.subscriberDevices) {
        ProvObjects::SubscriberDevice device;
        device.deviceGroup = entry.first;
        device.serialNumber = entry.second;
        devList.subscriberDevices.push_back(device);
    }
    return g_state.provGetDevicesOk;
}

} // namespace OpenWifi::SDK::Prov::Subscriber

namespace OpenWifi::SDK::Prov::Device {

bool Get(RESTAPIHandler *, const std::string &, ProvObjects::InventoryTag &device) {
    device = g_state.inventory;
    return g_state.inventoryOk;
}

} // namespace OpenWifi::SDK::Prov::Device

namespace OpenWifi::SDK::Prov::Venue {

bool Get(RESTAPIHandler *, const std::string &, ProvObjects::Venue &venue,
         Poco::Net::HTTPServerResponse::HTTPStatus &callStatus, Poco::JSON::Object::Ptr &callResponse) {
    venue = g_state.venue;
    callStatus = g_state.venueStatus;
    callResponse = g_state.venueResponse;
    return g_state.venueOk;
}

} // namespace OpenWifi::SDK::Prov::Venue

namespace OpenWifi::SDK::GW::Device {

bool GetConfig(RESTAPIHandler *, const std::string &, Poco::Net::HTTPResponse::HTTPStatus &responseStatus,
               Poco::JSON::Object::Ptr &response) {
    responseStatus = g_state.gatewayGetConfigStatus;
    response = g_state.gatewayGetConfigResponse;
    return g_state.gatewayGetConfigOk;
}

bool Configure(RESTAPIHandler *, const std::string &mac, Poco::JSON::Object::Ptr &configuration,
               Poco::Net::HTTPResponse::HTTPStatus &responseStatus, Poco::JSON::Object::Ptr &response) {
    g_state.configuredSerial = mac;
    g_state.configuredObject = configuration;
    responseStatus = g_state.gatewayConfigureStatus;
    response = g_state.gatewayConfigureResponse;
    return g_state.gatewayConfigureOk;
}

} // namespace OpenWifi::SDK::GW::Device

namespace OpenWifi::SDK::Topology {

bool Get(RESTAPIHandler *, const std::string &boardId, Poco::Net::HTTPServerResponse::HTTPStatus &callStatus,
         Poco::JSON::Object::Ptr &callResponse) {
    g_state.lastBoardId = boardId;
    callStatus = g_state.topologyStatus;
    callResponse = g_state.topologyResponse;
    return g_state.topologyOk;
}

} // namespace OpenWifi::SDK::Topology

#include "../../src/RESTAPI/RESTAPI_parental_control_utils.cpp"

namespace {

Poco::JSON::Object::Ptr MakeTopologyWithHistoricalClient(const std::string &mac) {
    auto client = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    client->set("station", mac);

    auto historicalClients = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    historicalClients->add(client);

    auto topology = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    topology->set("historicalClients", historicalClients);
    return topology;
}

Poco::JSON::Object::Ptr MakeTopologyWithHistoricalDevice(const std::string &mac) {
    auto historicalDevices = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    historicalDevices->add(mac);

    auto topology = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    topology->set("historicalDevices", historicalDevices);
    return topology;
}

Poco::JSON::Object::Ptr MakeTopologyWithNodesClient(const std::string &mac) {
    auto client = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    client->set("station", mac);
    auto clients = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    clients->add(client);

    auto ap = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    ap->set("clients", clients);
    auto aps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    aps->add(ap);

    auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    node->set("aps", aps);
    auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    nodes->add(node);

    auto topology = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    topology->set("nodes", nodes);
    return topology;
}


void TestParseTimeStringAcceptsMidnight() {
    int minuteOfDay = -1;
    Expect(OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(std::string("00:00")), minuteOfDay),
           "00:00 should parse");
    ExpectEq(minuteOfDay, 0, "midnight minute value");
}

void TestParseTimeStringAcceptsLastMinuteOfDay() {
    int minuteOfDay = -1;
    Expect(OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(std::string("23:59")), minuteOfDay),
           "23:59 should parse");
    ExpectEq(minuteOfDay, 1439, "23:59 minute value");
}

void TestParseTimeStringRejectsNonString() {
    int minuteOfDay = -1;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(7), minuteOfDay),
           "non-string time should fail");
}

void TestParseTimeStringRejectsMalformedString() {
    int minuteOfDay = -1;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(std::string("7:30")), minuteOfDay),
           "bad HH:MM formatting should fail");
}

void TestParseTimeStringRejectsHourOutOfRange() {
    int minuteOfDay = -1;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(std::string("24:00")), minuteOfDay),
           "24:00 should fail");
}

void TestParseTimeStringRejectsMinuteOutOfRange() {
    int minuteOfDay = -1;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ParseTimeString(Poco::Dynamic::Var(std::string("10:60")), minuteOfDay),
           "10:60 should fail");
}

void TestValidateWeekdaysAcceptsDistinctWeekdays() {
    Expect(OpenWifi::RESTAPI::ParentalControl::ValidateWeekdays(MakeIntArray({0, 2, 4, 6})),
           "distinct weekday values should pass");
}

void TestValidateWeekdaysRejectsEmptyArray() {
    Expect(!OpenWifi::RESTAPI::ParentalControl::ValidateWeekdays(Poco::JSON::Array::Ptr(new Poco::JSON::Array())),
           "empty weekday array should fail");
}

void TestValidateWeekdaysRejectsDuplicates() {
    Expect(!OpenWifi::RESTAPI::ParentalControl::ValidateWeekdays(MakeIntArray({1, 1, 2})),
           "duplicate weekday array should fail");
}

void TestValidateWeekdaysRejectsOutOfRangeValue() {
    Expect(!OpenWifi::RESTAPI::ParentalControl::ValidateWeekdays(MakeIntArray({0, 7})),
           "weekday outside 0..6 should fail");
}

void TestValidateWeekdaysRejectsNonIntegerValue() {
    auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arr->add("three");
    Expect(!OpenWifi::RESTAPI::ParentalControl::ValidateWeekdays(arr),
           "non-integer weekday entries should fail");
}

void TestNormalizeScheduleResponseConvertsMinuteFields() {
    auto schedule = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schedule->set("start_minute", 60);
    schedule->set("stop_minute", 90);
    schedule->set("config-raw", MakeStringArray({"remove-me"}));

    Expect(OpenWifi::RESTAPI::ParentalControl::NormalizeScheduleResponse(schedule),
           "schedule normalization should succeed");
    ExpectEq(schedule->getValue<std::string>("start_time"), std::string("01:00"), "start_time conversion");
    ExpectEq(schedule->getValue<std::string>("stop_time"), std::string("01:30"), "stop_time conversion");
    Expect(!schedule->has("start_minute"), "start_minute should be removed");
    Expect(!schedule->has("stop_minute"), "stop_minute should be removed");
    Expect(!schedule->has("config-raw"), "config-raw should be removed");
}

void TestNormalizeScheduleResponseRejectsMissingMinuteFields() {
    auto schedule = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schedule->set("start_minute", 60);
    Expect(!OpenWifi::RESTAPI::ParentalControl::NormalizeScheduleResponse(schedule),
           "missing stop_minute should fail");
}

void TestNormalizeScheduleResponseRejectsOutOfRangeMinute() {
    auto schedule = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schedule->set("start_minute", -1);
    schedule->set("stop_minute", 90);
    Expect(!OpenWifi::RESTAPI::ParentalControl::NormalizeScheduleResponse(schedule),
           "negative minute should fail");
}

void TestNormalizeScheduleResponseRejectsNonIntegerField() {
    auto schedule = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    schedule->set("start_minute", "sixty");
    schedule->set("stop_minute", 90);
    Expect(!OpenWifi::RESTAPI::ParentalControl::NormalizeScheduleResponse(schedule),
           "non-integer minute field should fail");
}

void TestBuildScheduleRequestBodyForAppTarget() {
    OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
    request.name = "Homework";
    request.description = std::string("School apps");
    request.enabled = true;
    request.targetKind = "APP";
    request.targetValue = "youtube";
    request.startMinute = 480;
    request.stopMinute = 960;
    request.weekdays = MakeIntArray({1, 2, 3});

    auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
    ExpectEq(body.getValue<std::string>("name"), std::string("Homework"), "name should be preserved");
    ExpectEq(body.getValue<std::string>("target_kind"), std::string("APP"), "APP target kind");
    ExpectEq(body.getValue<std::string>("target_value"), std::string("youtube"), "APP target value");
}

void TestBuildScheduleRequestBodyForInternetTargetOmittedAndExplicitNull() {
    // 1. Omitted case
    {
        OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
        request.name = "Internet";
        request.description = std::string("General block");
        request.has_description = true;
        request.enabled = false;
        request.targetKind = "INTERNET";
        request.has_target_value = false;
        request.startMinute = 0;
        request.stopMinute = 60;
        request.weekdays = MakeIntArray({0});

        auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
        Expect(!body.has("target_value"), "omitted INTERNET target value should not be present in payload");
    }

    // 2. Explicit null case
    {
        OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
        request.name = "Internet";
        request.description = std::string("General block");
        request.has_description = true;
        request.enabled = false;
        request.targetKind = "INTERNET";
        request.has_target_value = true;
        request.startMinute = 0;
        request.stopMinute = 60;
        request.weekdays = MakeIntArray({0});

        auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
        Expect(body.has("target_value") && body.isNull("target_value"), "explicitly null INTERNET target value should be null in payload");
    }
}

void TestBuildScheduleRequestBodyDescriptionOmittedAndExplicitNull() {
    // 1. Omitted case
    {
        OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
        request.name = "No description";
        request.has_description = false;
        request.enabled = true;
        request.targetKind = "INTERNET";
        request.startMinute = 10;
        request.stopMinute = 20;
        request.weekdays = MakeIntArray({0, 1});

        auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
        Expect(!body.has("description"), "omitted description should not be present in payload");
    }

    // 2. Explicit null case
    {
        OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
        request.name = "Null description";
        request.has_description = true;
        request.description = std::nullopt;
        request.enabled = true;
        request.targetKind = "INTERNET";
        request.startMinute = 10;
        request.stopMinute = 20;
        request.weekdays = MakeIntArray({0, 1});

        auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
        Expect(body.has("description") && body.isNull("description"), "explicitly null description should be null in payload");
    }
}

void TestExtractConfigRawSnapshotAllowsMissingResponseWhenOptional() {
    Poco::JSON::Array::Ptr configRaw;
    Expect(OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(nullptr, configRaw, false),
           "optional missing response should pass");
    Expect(!configRaw, "config-raw should remain null");
}

void TestExtractConfigRawSnapshotRejectsMissingResponseWhenRequired() {
    Poco::JSON::Array::Ptr configRaw;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(nullptr, configRaw, true),
           "required missing response should fail");
}

void TestExtractConfigRawSnapshotAcceptsNullConfigRaw() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    response->set("config-raw", Poco::Dynamic::Var());
    Poco::JSON::Array::Ptr configRaw;

    Expect(OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(response, configRaw, true),
           "null config-raw should be accepted");
    Expect(!configRaw, "config-raw should remain null when response config-raw is null");
}

void TestExtractConfigRawSnapshotAcceptsValidCommands() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "foo"}));
    configRaw->add(MakeStringArray({"set", "bar", "baz"}));
    response->set("config-raw", configRaw);

    Poco::JSON::Array::Ptr extracted;
    Expect(OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(response, extracted, true),
           "well-formed config-raw should pass");
    Expect(extracted && extracted->size() == 2, "config-raw should be returned");
}

void TestExtractConfigRawSnapshotRejectsInvalidCommandLength() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"only-one"}));
    response->set("config-raw", configRaw);

    Poco::JSON::Array::Ptr extracted;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(response, extracted, true),
           "command arrays must have length 2 or 3");
}

void TestExtractConfigRawSnapshotRejectsNonStringCommandEntry() {
    auto response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto badCommand = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    badCommand->add("set");
    badCommand->add(123);
    configRaw->add(badCommand);
    response->set("config-raw", configRaw);

    Poco::JSON::Array::Ptr extracted;
    Expect(!OpenWifi::RESTAPI::ParentalControl::ExtractConfigRawSnapshot(response, extracted, true),
           "command entries must all be strings");
}

void TestValidateMacInTopologySuccessHistoricalClient() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.topologyResponse = MakeTopologyWithHistoricalClient("11:22:33:44:55:66");

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::Success,
        "should succeed for historical client MAC"
    );
    ExpectEq(gatewaySerial, std::string("112233445566"), "resolved gateway serial");
}

void TestValidateMacInTopologySuccessHistoricalDevice() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.topologyResponse = MakeTopologyWithHistoricalDevice("11:22:33:44:55:66");

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::Success,
        "should succeed for historical device MAC"
    );
}

void TestValidateMacInTopologySuccessNodesClient() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.topologyResponse = MakeTopologyWithNodesClient("11:22:33:44:55:66");

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::Success,
        "should succeed for nodes active client MAC"
    );
}

void TestValidateMacInTopologyMacNotPresent() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.topologyResponse = MakeTopologyWithHistoricalClient("66:55:44:33:22:11");

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
        "should fail when MAC is not in topology"
    );
}

void TestValidateMacInTopologyProvisioningLookupFailure() {
    g_state.provGetDevicesOk = false;
    g_state.provStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::ProvisioningLookupFailed,
        "should fail when provisioning GetDevices fails"
    );
}

void TestValidateMacInTopologyInventoryMissing() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.inventoryOk = false;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::InventoryNotFound,
        "should fail when inventory lookup fails"
    );
}

void TestValidateMacInTopologyVenueIdMissing() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.inventory.venue = ""; // missing venue in inventory

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::VenueNotFound,
        "should fail when venue id is missing in inventory"
    );
}

void TestValidateMacInTopologyVenueNotFound() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.venueOk = false;
    g_state.venueStatus = Poco::Net::HTTPResponse::HTTP_NOT_FOUND;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::VenueNotFound,
        "should fail with VenueNotFound when venue lookup returns 404"
    );
}

void TestValidateMacInTopologyVenueLookupFailure() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.venueOk = false;
    g_state.venueStatus = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::VenueLookupFailed,
        "should fail with VenueLookupFailed when venue lookup fails with 5xx"
    );
}

void TestValidateMacInTopologyBoardMissing() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.venue.boards = {}; // empty boards

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::BoardIdNotFound,
        "should fail when board is missing/empty in venue"
    );
}

void TestValidateMacInTopologyTopologyFetchFailure() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    g_state.topologyOk = false;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyNotFound,
        "should fail when topology lookup fails"
    );
}

void TestValidateMacInTopologyMalformedTopology() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    malformed->set("nodes", "not-an-array"); // malformed shape
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail for malformed topology payload"
    );
}

void TestValidateMacInTopologyMalformedNoFields() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when topology has none of nodes, historicalClients, historicalDevices"
    );
}

void TestValidateMacInTopologyMalformedHistoricalClientsNotArray() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    malformed->set("historicalClients", 12345);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when historicalClients is not an array"
    );
}

void TestValidateMacInTopologyMalformedHistoricalDevicesNotArray() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    malformed->set("historicalDevices", "not-an-array");
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when historicalDevices is not an array"
    );
}

void TestValidateMacInTopologyMalformedHistoricalClientsElementNotObject() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arr->add("not-an-object");
    malformed->set("historicalClients", arr);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when historicalClients contains non-object element"
    );
}

void TestValidateMacInTopologyMalformedHistoricalDevicesElementNotString() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    arr->add(12345); // not a string
    malformed->set("historicalDevices", arr);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when historicalDevices contains non-string element"
    );
}

void TestValidateMacInTopologyMalformedNodesApsNotArray() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    node->set("aps", "not-an-array");
    nodes->add(node);
    malformed->set("nodes", nodes);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when nodes/aps is not an array"
    );
}

void TestValidateMacInTopologyMalformedNodesApsElementNotObject() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto aps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    aps->add("not-an-object");
    node->set("aps", aps);
    nodes->add(node);
    malformed->set("nodes", nodes);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when nodes/aps contains non-object element"
    );
}

void TestValidateMacInTopologyMalformedNodesApsClientsNotArray() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};
    auto malformed = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto aps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto ap = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    ap->set("clients", 12345); // not an array
    aps->add(ap);
    node->set("aps", aps);
    nodes->add(node);
    malformed->set("nodes", nodes);
    g_state.topologyResponse = malformed;

    std::string gatewaySerial;
    FakeResponse response;
    FakeRequest request("GET", "/test", "", response);
    FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

    ExpectEq(
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
        (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
        "should fail when nodes/aps/clients is not an array"
    );
}

void TestValidateMacInTopologyNullArrays() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};

    // 1. All three arrays are null
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("nodes", Poco::Dynamic::Var());
        topo->set("historicalClients", Poco::Dynamic::Var());
        topo->set("historicalDevices", Poco::Dynamic::Var());
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "all null arrays should return TopologyUnusable"
        );
    }

    // 2. Nodes is empty array, historicalClients/historicalDevices are null
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("nodes", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
        topo->set("historicalClients", Poco::Dynamic::Var());
        topo->set("historicalDevices", Poco::Dynamic::Var());
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
            "empty nodes array with other nulls should return MacNotPresentInTopology"
        );
    }

    // 3. historicalClients is empty array, nodes/historicalDevices are null
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("nodes", Poco::Dynamic::Var());
        topo->set("historicalClients", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
        topo->set("historicalDevices", Poco::Dynamic::Var());
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
            "empty historicalClients array with other nulls should return MacNotPresentInTopology"
        );
    }

    // 4. historicalDevices is empty array, nodes/historicalClients are null
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("nodes", Poco::Dynamic::Var());
        topo->set("historicalClients", Poco::Dynamic::Var());
        topo->set("historicalDevices", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
            "empty historicalDevices array with other nulls should return MacNotPresentInTopology"
        );
    }
}

void TestValidateMacInTopologyNestedNullApsAndClients() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};

    // 1. aps is explicitly null inside a node
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        node->set("aps", Poco::Dynamic::Var()); // explicitly null
        nodes->add(node);
        topo->set("nodes", nodes);
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
            "explicitly null nested aps should be tolerated and return MacNotPresentInTopology"
        );
    }

    // 2. clients is explicitly null inside an ap
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto aps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto ap = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        ap->set("clients", Poco::Dynamic::Var()); // explicitly null
        aps->add(ap);
        node->set("aps", aps);
        nodes->add(node);
        topo->set("nodes", nodes);
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::MacNotPresentInTopology,
            "explicitly null nested clients should be tolerated and return MacNotPresentInTopology"
        );
    }
}

void TestValidateMacInTopologyNegativeNonNullNonArray() {
    g_state.subscriberDevices = {{"olg", "112233445566"}};

    // 1. nodes is boolean
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("nodes", true);
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "non-array nodes should return TopologyUnusable"
        );
    }

    // 2. historicalClients is string
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("historicalClients", "not-an-array");
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "non-array historicalClients should return TopologyUnusable"
        );
    }

    // 3. historicalDevices is object
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        topo->set("historicalDevices", Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "non-array historicalDevices should return TopologyUnusable"
        );
    }

    // 4. Nested aps is number
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        node->set("aps", 42); // not an array
        nodes->add(node);
        topo->set("nodes", nodes);
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "non-array nested aps should return TopologyUnusable"
        );
    }

    // 5. Nested clients is object
    {
        auto topo = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto nodes = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto node = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        auto aps = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        auto ap = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        ap->set("clients", Poco::JSON::Object::Ptr(new Poco::JSON::Object())); // not an array
        aps->add(ap);
        node->set("aps", aps);
        nodes->add(node);
        topo->set("nodes", nodes);
        g_state.topologyResponse = topo;

        std::string gatewaySerial;
        FakeResponse response;
        FakeRequest request("GET", "/test", "", response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, &response);

        ExpectEq(
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacInTopology(handler, "sub-1", "op-1", "112233445566", gatewaySerial),
            (int)OpenWifi::RESTAPI::ParentalControl::ValidateMacResult::TopologyUnusable,
            "non-array nested clients should return TopologyUnusable"
        );
    }
}

void TestHandleValidateMacResult() {
    using namespace OpenWifi::RESTAPI::ParentalControl;

    // Helper lambda to test individual mapping
    auto verifyMapping = [](ValidateMacResult result, Poco::Net::HTTPResponse::HTTPStatus expectedHTTPStatus, int expectedErrorCode) {
        auto *response = new FakeResponse();
        FakeRequest request("GET", "/test", "", *response);
        FakeRESTAPIHandler handler(Poco::Logger::get("test"), &request, response);

        bool success = HandleValidateMacResult(handler, result);
        if (result == ValidateMacResult::Success) {
            Expect(success, "Success should return true");
            ExpectEq((int)response->getStatus(), (int)Poco::Net::HTTPResponse::HTTP_OK, "Success status is 200 OK");
            Expect(response->body().empty(), "Success body should be empty");
        } else {
            Expect(!success, "Failure result should return false");
            ExpectEq((int)response->getStatus(), (int)expectedHTTPStatus, "HTTP Status mismatch");
            
            Poco::JSON::Parser parser;
            auto parsed = parser.parse(response->body()).extract<Poco::JSON::Object::Ptr>();
            
            if (expectedHTTPStatus == Poco::Net::HTTPResponse::HTTP_FORBIDDEN) {
                ExpectEq(parsed->getValue<int>("ErrorCode"), expectedErrorCode, "ErrorCode mismatch for UnAuthorized");
            } else if (expectedHTTPStatus == Poco::Net::HTTPResponse::HTTP_BAD_REQUEST) {
                ExpectEq(parsed->getValue<int>("ErrorCode"), 400, "ErrorCode mismatch for BadRequest");
            } else if (expectedHTTPStatus == Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR) {
                ExpectEq(parsed->getValue<int>("ErrorCode"), 500, "ErrorCode mismatch for InternalError");
            }

            std::string desc = parsed->getValue<std::string>("ErrorDescription");
            auto colonPos = desc.find(':');
            Expect(colonPos != std::string::npos, "ErrorDescription must contain ':' separator");
            int actualSpecificCode = std::stoi(desc.substr(0, colonPos));
            ExpectEq(actualSpecificCode, expectedErrorCode, "Specific error code mismatch in ErrorDescription");
        }
        delete response;
    };

    verifyMapping(ValidateMacResult::Success, Poco::Net::HTTPResponse::HTTP_OK, 0);
    verifyMapping(ValidateMacResult::MissingSubscriberOrOperator, Poco::Net::HTTPResponse::HTTP_FORBIDDEN, 1066);
    verifyMapping(ValidateMacResult::SubscriberDevicesNotFound, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1100);
    verifyMapping(ValidateMacResult::GatewaySerialNotFound, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1100);
    verifyMapping(ValidateMacResult::ProvisioningLookupFailed, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, 1002);
    verifyMapping(ValidateMacResult::InventoryNotFound, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1199);
    verifyMapping(ValidateMacResult::VenueNotFound, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1023);
    verifyMapping(ValidateMacResult::VenueLookupFailed, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, 1002);
    verifyMapping(ValidateMacResult::BoardIdNotFound, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1200);
    verifyMapping(ValidateMacResult::TopologyNotFound, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, 1002);
    verifyMapping(ValidateMacResult::MacNotPresentInTopology, Poco::Net::HTTPResponse::HTTP_BAD_REQUEST, 1198);
    verifyMapping(ValidateMacResult::TopologyUnusable, Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, 1002);
}

void TestGetBlockedClientsClientAccessExpiredRuleSameDay() {
    Poco::DateTime now;
    std::string todayDate = FormatDate(now);
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65.start_date", todayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65.stop_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_6C_C7_EC_DE_10_65.stop_time", "00:00:01"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_6C_C7_EC_DE_10_65.src_mac", "6c:c7:ec:de:10:65"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    Expect(blockedMacs.empty(), "Expired client access rule (00:00:01) should not block client");
}

void TestGetBlockedClientsClientAccessActiveRule() {
    Poco::DateTime now;
    std::string todayDate = FormatDate(now);
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_date", todayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_10_F1_F2_86_11_DC.src_mac", "10:f1:f2:86:11:dc"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    ExpectEq(blockedMacs.size(), static_cast<std::size_t>(1), "Active rule should produce 1 blocked MAC");
}

void TestGetBlockedClientsGroupScheduleRuleIgnored() {
    Poco::DateTime now;
    static const std::vector<std::string> names = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::string todayWeekday = names[now.dayOfWeek()];

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.weekdays", todayWeekday}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.stop_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.src_mac", "5a:f8:57:ca:a5:3e"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    Expect(blockedMacs.empty(), "Topology blocked clients should ignore active group schedule rules");
}

void TestGetBlockedClientsMixedRulesOnlyClientAccessBlocks() {
    Poco::DateTime now;
    std::string todayDate = FormatDate(now);
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));
    static const std::vector<std::string> names = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    std::string todayWeekday = names[now.dayOfWeek()];

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_date", todayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_10_F1_F2_86_11_DC.src_mac", "10:f1:f2:86:11:dc"}));

    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.weekdays", todayWeekday}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.stop_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_rule_g1_s1_5a_f8_57_ca_a5_3e.src_mac", "5a:f8:57:ca:a5:3e"}));

    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    ExpectEq(blockedMacs.size(), static_cast<std::size_t>(1), "Only active client-access rules should affect topology blocked clients");
}

void TestGetBlockedClientsDisabledRulesIgnored() {
    Poco::DateTime now;
    std::string todayDate = FormatDate(now);
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.enabled", "0"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_date", todayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.start_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_10_F1_F2_86_11_DC.stop_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_10_F1_F2_86_11_DC.src_mac", "10:f1:f2:86:11:dc"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    Expect(blockedMacs.empty(), "Disabled rule (enabled=0) should be ignored");
}

// =========================================================================
// Client-access time window tests
// =========================================================================

void TestGetBlockedClientsClientAccessOvernightWindowActive() {
    Poco::DateTime now;
    std::string yesterdayDate = FormatDate(ShiftDateTime(now, -1));
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_date", yesterdayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.src_mac", "5a:f8:57:ca:a5:3e"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    ExpectEq(blockedMacs.size(), static_cast<std::size_t>(1), "Active overnight client-access window should block client");
}

void TestGetBlockedClientsClientAccessOvernightFutureWindowInactive() {
    Poco::DateTime now;
    std::string tomorrowDate = FormatDate(ShiftDateTime(now, 1));
    std::string dayAfterTomorrowDate = FormatDate(ShiftDateTime(now, 2));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_date", tomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_date", dayAfterTomorrowDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.src_mac", "5a:f8:57:ca:a5:3e"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    Expect(blockedMacs.empty(), "Future overnight client-access window should not block client yet");
}

void TestGetBlockedClientsClientAccessOvernightExpiredWindowInactive() {
    Poco::DateTime now;
    std::string twoDaysAgoDate = FormatDate(ShiftDateTime(now, -2));
    std::string yesterdayDate = FormatDate(ShiftDateTime(now, -1));

    auto config = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto configRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E", "rule"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.enabled", "1"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_date", twoDaysAgoDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_date", yesterdayDate}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.start_time", "23:59:59"}));
    configRaw->add(MakeStringArray({"set", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.stop_time", "00:00:00"}));
    configRaw->add(MakeStringArray({"add_list", "firewall.pc_client_access_5A_F8_57_CA_A5_3E.src_mac", "5a:f8:57:ca:a5:3e"}));
    config->set("config-raw", configRaw);

    std::list<std::string> blockedMacs;
    Expect(OpenWifi::RESTAPI::ParentalControl::GetBlockedClients(config, blockedMacs), "GetBlockedClients should return true");
    Expect(blockedMacs.empty(), "Expired overnight client-access window should not block client");
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"ValidateMacInTopologySuccessHistoricalClient", TestValidateMacInTopologySuccessHistoricalClient},
    {"ValidateMacInTopologySuccessHistoricalDevice", TestValidateMacInTopologySuccessHistoricalDevice},
    {"ValidateMacInTopologySuccessNodesClient", TestValidateMacInTopologySuccessNodesClient},
    {"ValidateMacInTopologyMacNotPresent", TestValidateMacInTopologyMacNotPresent},
    {"ValidateMacInTopologyProvisioningLookupFailure", TestValidateMacInTopologyProvisioningLookupFailure},
    {"ValidateMacInTopologyInventoryMissing", TestValidateMacInTopologyInventoryMissing},
    {"ValidateMacInTopologyVenueIdMissing", TestValidateMacInTopologyVenueIdMissing},
    {"ValidateMacInTopologyVenueNotFound", TestValidateMacInTopologyVenueNotFound},
    {"ValidateMacInTopologyVenueLookupFailure", TestValidateMacInTopologyVenueLookupFailure},
    {"ValidateMacInTopologyBoardMissing", TestValidateMacInTopologyBoardMissing},
    {"ValidateMacInTopologyTopologyFetchFailure", TestValidateMacInTopologyTopologyFetchFailure},
    {"ValidateMacInTopologyMalformedTopology", TestValidateMacInTopologyMalformedTopology},
    {"ValidateMacInTopologyMalformedNoFields", TestValidateMacInTopologyMalformedNoFields},
    {"ValidateMacInTopologyMalformedHistoricalClientsNotArray", TestValidateMacInTopologyMalformedHistoricalClientsNotArray},
    {"ValidateMacInTopologyMalformedHistoricalDevicesNotArray", TestValidateMacInTopologyMalformedHistoricalDevicesNotArray},
    {"ValidateMacInTopologyMalformedHistoricalClientsElementNotObject", TestValidateMacInTopologyMalformedHistoricalClientsElementNotObject},
    {"ValidateMacInTopologyMalformedHistoricalDevicesElementNotString", TestValidateMacInTopologyMalformedHistoricalDevicesElementNotString},
    {"ValidateMacInTopologyMalformedNodesApsNotArray", TestValidateMacInTopologyMalformedNodesApsNotArray},
    {"ValidateMacInTopologyMalformedNodesApsElementNotObject", TestValidateMacInTopologyMalformedNodesApsElementNotObject},
    {"ValidateMacInTopologyMalformedNodesApsClientsNotArray", TestValidateMacInTopologyMalformedNodesApsClientsNotArray},
    {"ValidateMacInTopologyNullArrays", TestValidateMacInTopologyNullArrays},
    {"ValidateMacInTopologyNestedNullApsAndClients", TestValidateMacInTopologyNestedNullApsAndClients},
    {"ValidateMacInTopologyNegativeNonNullNonArray", TestValidateMacInTopologyNegativeNonNullNonArray},
    {"HandleValidateMacResult", TestHandleValidateMacResult},
    {"ParseTimeStringAcceptsMidnight", TestParseTimeStringAcceptsMidnight},
    {"ParseTimeStringAcceptsLastMinuteOfDay", TestParseTimeStringAcceptsLastMinuteOfDay},
    {"ParseTimeStringRejectsNonString", TestParseTimeStringRejectsNonString},
    {"ParseTimeStringRejectsMalformedString", TestParseTimeStringRejectsMalformedString},
    {"ParseTimeStringRejectsHourOutOfRange", TestParseTimeStringRejectsHourOutOfRange},
    {"ParseTimeStringRejectsMinuteOutOfRange", TestParseTimeStringRejectsMinuteOutOfRange},
    {"ValidateWeekdaysAcceptsDistinctWeekdays", TestValidateWeekdaysAcceptsDistinctWeekdays},
    {"ValidateWeekdaysRejectsEmptyArray", TestValidateWeekdaysRejectsEmptyArray},
    {"ValidateWeekdaysRejectsDuplicates", TestValidateWeekdaysRejectsDuplicates},
    {"ValidateWeekdaysRejectsOutOfRangeValue", TestValidateWeekdaysRejectsOutOfRangeValue},
    {"ValidateWeekdaysRejectsNonIntegerValue", TestValidateWeekdaysRejectsNonIntegerValue},
    {"NormalizeScheduleResponseConvertsMinuteFields", TestNormalizeScheduleResponseConvertsMinuteFields},
    {"NormalizeScheduleResponseRejectsMissingMinuteFields", TestNormalizeScheduleResponseRejectsMissingMinuteFields},
    {"NormalizeScheduleResponseRejectsOutOfRangeMinute", TestNormalizeScheduleResponseRejectsOutOfRangeMinute},
    {"NormalizeScheduleResponseRejectsNonIntegerField", TestNormalizeScheduleResponseRejectsNonIntegerField},
    {"BuildScheduleRequestBodyForAppTarget", TestBuildScheduleRequestBodyForAppTarget},
    {"BuildScheduleRequestBodyForInternetTargetOmittedAndExplicitNull", TestBuildScheduleRequestBodyForInternetTargetOmittedAndExplicitNull},
    {"BuildScheduleRequestBodyDescriptionOmittedAndExplicitNull", TestBuildScheduleRequestBodyDescriptionOmittedAndExplicitNull},
    {"ExtractConfigRawSnapshotAllowsMissingResponseWhenOptional", TestExtractConfigRawSnapshotAllowsMissingResponseWhenOptional},
    {"ExtractConfigRawSnapshotRejectsMissingResponseWhenRequired", TestExtractConfigRawSnapshotRejectsMissingResponseWhenRequired},
    {"ExtractConfigRawSnapshotAcceptsNullConfigRaw", TestExtractConfigRawSnapshotAcceptsNullConfigRaw},
    {"ExtractConfigRawSnapshotAcceptsValidCommands", TestExtractConfigRawSnapshotAcceptsValidCommands},
    {"ExtractConfigRawSnapshotRejectsInvalidCommandLength", TestExtractConfigRawSnapshotRejectsInvalidCommandLength},
    {"ExtractConfigRawSnapshotRejectsNonStringCommandEntry", TestExtractConfigRawSnapshotRejectsNonStringCommandEntry},
    {"GetBlockedClientsClientAccessExpiredRuleSameDay", TestGetBlockedClientsClientAccessExpiredRuleSameDay},
    {"GetBlockedClientsClientAccessActiveRule", TestGetBlockedClientsClientAccessActiveRule},
    {"GetBlockedClientsGroupScheduleRuleIgnored", TestGetBlockedClientsGroupScheduleRuleIgnored},
    {"GetBlockedClientsMixedRulesOnlyClientAccessBlocks", TestGetBlockedClientsMixedRulesOnlyClientAccessBlocks},
    {"GetBlockedClientsDisabledRulesIgnored", TestGetBlockedClientsDisabledRulesIgnored},
    {"GetBlockedClientsClientAccessOvernightWindowActive", TestGetBlockedClientsClientAccessOvernightWindowActive},
    {"GetBlockedClientsClientAccessOvernightFutureWindowInactive", TestGetBlockedClientsClientAccessOvernightFutureWindowInactive},
    {"GetBlockedClientsClientAccessOvernightExpiredWindowInactive", TestGetBlockedClientsClientAccessOvernightExpiredWindowInactive},
};

} // namespace

int main() {
    int failures = 0;
    for (const auto &test : kTests) {
        try {
            ResetStubs();
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
