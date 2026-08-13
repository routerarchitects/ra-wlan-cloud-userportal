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

    bool extractConfigRawOk = true;
    Poco::JSON::Array::Ptr extractedConfigRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult applyResult =
        OpenWifi::RESTAPI::ParentalControl::ApplyConfigRawResult::Applied;

    int createCalls = 0;
    int deleteCalls = 0;
    std::string lastSubscriberId;
    std::string lastOperatorId;
    std::string lastGroupId;
    std::string lastObjectType;
    bool lastConfigRawRequired = false;
    OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse lastSuccessResponse =
        OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse::Ok;
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

bool ValidateAuthPreconditions(RESTAPIHandler &handler, const std::string &subscriberId, const std::string &operatorId, bool requireOperatorId) {
    if (subscriberId.empty()) {
        handler.UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
        return false;
    }
    if (requireOperatorId && operatorId.empty()) {
        handler.UnAuthorized(RESTAPI::Errors::OperatorIdMustExist);
        return false;
    }
    return true;
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

bool NormalizeScheduleResponse(Poco::JSON::Object::Ptr schedule, const std::string &timezone) {
    (void)schedule;
    (void)timezone;
    return true;
}

void ForwardParentalControlErrorResponse(RESTAPIHandler *handler,
                                        Poco::Net::HTTPResponse::HTTPStatus status,
                                        const Poco::JSON::Object::Ptr &downstreamResponse) {
    if (handler != nullptr) {
        handler->ForwardErrorResponse(handler, status, downstreamResponse);
    }
}


void HandleParentalControlMutationResult(RESTAPIHandler &handler,
                                         Poco::Logger &logger,
                                         const MutationCallResult &mutation,
                                         const std::string &subscriberId,
                                         const std::string &operatorId,
                                         const std::string &applyTargetId,
                                         const std::string &operationName,
                                         const std::string &objectType,
                                         bool configRawRequired,
                                         const std::string &,
                                         MutationSuccessResponse successResponse,
                                         const std::string &) {
    (void)logger;
    (void)operationName;
    g_state.lastSubscriberId = subscriberId;
    g_state.lastOperatorId = operatorId;
    g_state.lastGroupId = applyTargetId;
    g_state.lastObjectType = objectType;
    g_state.lastConfigRawRequired = configRawRequired;
    g_state.lastSuccessResponse = successResponse;

    if (!mutation.success) {
        ForwardParentalControlErrorResponse(&handler, mutation.status, mutation.response);
        return;
    }
    Poco::JSON::Object::Ptr response = mutation.response;
    if (successResponse == MutationSuccessResponse::Ok) {
        return handler.OK();
    }
    if (response) {
        if (successResponse == MutationSuccessResponse::ReturnObjectWithoutConfigRaw && response->has("config-raw")) {
            response->remove("config-raw");
        }
        return handler.ReturnObject(*response);
    }
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
            ExpectEq(g_state.lastConfigRawRequired, true, "configRawRequired should be true");
            ExpectEq(g_state.lastGroupId, kValidGroupId, "applyTargetId should be group_id");
            ExpectEq(g_state.lastObjectType, std::string("group_device"), "objectType should be group_device");
            Expect(g_state.lastSuccessResponse == OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse::ReturnObjectWithoutConfigRaw, "successResponse should be ReturnObjectWithoutConfigRaw");
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
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.lastConfigRawRequired, true, "configRawRequired should be true");
            ExpectEq(g_state.lastGroupId, kValidGroupId, "applyTargetId should be group_id");
            ExpectEq(g_state.lastObjectType, std::string("group_device"), "objectType should be group_device");
            Expect(g_state.lastSuccessResponse == OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse::Ok, "successResponse should be Ok");
        }
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
