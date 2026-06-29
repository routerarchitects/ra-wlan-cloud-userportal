/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_group_devices_handler.h"
#include "RESTAPI/RESTAPI_group_devices_list_handler.h"

namespace {

const std::string kValidGroupId = "11111111-1111-4111-8111-111111111111";
const std::string kInvalidGroupId = "bad-group-id";
const std::string kValidMac = "AA:BB:CC:DD:EE:FF";
const std::string kInvalidMac = "invalid-mac";

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
    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/devices",
        "",
        {{"group_id", kValidGroupId}},
        "",
        "",
        Poco::Net::HTTPResponse::HTTP_FORBIDDEN
    );
}

void TestListGetRejectsInvalidGroupId() {
    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/devices",
        "",
        {{"group_id", kInvalidGroupId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST
    );
}

void TestListGetReturnsJSONArrayOnSuccess() {
    auto device = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    device->set("client_mac", kValidMac);
    g_state.getListArray->add(device);

    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/devices",
        "",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(array->size(), static_cast<std::size_t>(1), "GET list should return one device");
            ExpectEq(g_state.lastGroupId, kValidGroupId, "group id should be forwarded to SDK");
            ExpectEq(g_state.lastSubscriberId, std::string("subscriber-1"), "subscriber id should be forwarded to SDK");
        }
    );
}

void TestPostRejectsMissingOwner() {
    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v1/groups/x/devices",
        "{\"client_mac\":\"AA:BB:CC:DD:EE:FF\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_FORBIDDEN,
        [](TestGroupDevicesListHandler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_mac", kValidMac);
            handler.setParsedBody(body);
        }
    );
}

void TestPostRejectsInvalidClientMac() {
    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v1/groups/x/devices",
        "{\"client_mac\":\"invalid\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListHandler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_mac", kInvalidMac);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.validateCalls, 0, "topology validation should not run for invalid MAC");
            ExpectEq(g_state.createCalls, 0, "SDK create should not run for invalid MAC");
        }
    );
}

void TestPostStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("client_mac", kValidMac);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createResponse = responseObject;

    RunHandlerRequest<TestGroupDevicesListHandler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v1/groups/x/devices",
        "{\"client_mac\":\"AA:BB:CC:DD:EE:FF\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupDevicesListHandler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_mac", kValidMac);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            auto parsed = ParseObject(response.body());
            Expect(!parsed->has("config-raw"), "POST response should strip config-raw");
            ExpectEq(parsed->getValue<std::string>("client_mac"), std::string(kValidMac), "client_mac should remain in response");
            ExpectEq(g_state.lastGatewaySerial, std::string("GW-123"), "gateway serial should be passed to ApplyConfigRaw");
        }
    );
}

void TestDeleteReturnsOkOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.deleteResponse = responseObject;

    RunHandlerRequest<TestGroupDevicesHandler>(
        Poco::Net::HTTPRequest::HTTP_DELETE,
        "/api/v1/groups/x/devices/y",
        "",
        {{"group_id", kValidGroupId}, {"client_mac", kValidMac}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK
    );
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

