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

void TestBuildScheduleRequestBodyForInternetTargetSetsNullTargetValue() {
    OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
    request.name = "Internet";
    request.description = std::string("General block");
    request.enabled = false;
    request.targetKind = "INTERNET";
    request.startMinute = 0;
    request.stopMinute = 60;
    request.weekdays = MakeIntArray({0});

    auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
    Expect(body.isNull("target_value"), "INTERNET target value should be null");
}

void TestBuildScheduleRequestBodyUsesNullDescriptionWhenOmitted() {
    OpenWifi::RESTAPI::ParentalControl::ParsedScheduleRequest request;
    request.name = "No description";
    request.enabled = true;
    request.targetKind = "INTERNET";
    request.startMinute = 10;
    request.stopMinute = 20;
    request.weekdays = MakeIntArray({0, 1});

    auto body = OpenWifi::RESTAPI::ParentalControl::BuildScheduleRequestBody(request);
    Expect(body.isNull("description"), "omitted description should serialize as null");
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

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
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
    {"BuildScheduleRequestBodyForInternetTargetSetsNullTargetValue", TestBuildScheduleRequestBodyForInternetTargetSetsNullTargetValue},
    {"BuildScheduleRequestBodyUsesNullDescriptionWhenOmitted", TestBuildScheduleRequestBodyUsesNullDescriptionWhenOmitted},
    {"ExtractConfigRawSnapshotAllowsMissingResponseWhenOptional", TestExtractConfigRawSnapshotAllowsMissingResponseWhenOptional},
    {"ExtractConfigRawSnapshotRejectsMissingResponseWhenRequired", TestExtractConfigRawSnapshotRejectsMissingResponseWhenRequired},
    {"ExtractConfigRawSnapshotAcceptsNullConfigRaw", TestExtractConfigRawSnapshotAcceptsNullConfigRaw},
    {"ExtractConfigRawSnapshotAcceptsValidCommands", TestExtractConfigRawSnapshotAcceptsValidCommands},
    {"ExtractConfigRawSnapshotRejectsInvalidCommandLength", TestExtractConfigRawSnapshotRejectsInvalidCommandLength},
    {"ExtractConfigRawSnapshotRejectsNonStringCommandEntry", TestExtractConfigRawSnapshotRejectsNonStringCommandEntry},
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
    bool AllowExternalMicroServices() { return false; }
}

namespace OpenWifi::Utils {
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
