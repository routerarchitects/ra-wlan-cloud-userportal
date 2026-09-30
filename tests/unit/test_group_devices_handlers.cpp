/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_group_devices_handler.h"
#include "RESTAPI/RESTAPI_group_devices_list_handler.h"
#include "RESTAPI/RESTAPI_group_devices_list_v2_handler.h"

namespace {

const std::string kValidGroupId = "11111111-1111-4111-8111-111111111111";
const std::string kInvalidGroupId = "bad-group-id";
const std::string kValidMac = "AA:BB:CC:DD:EE:FF";
const std::string kValidMac2 = "AA:BB:CC:DD:EE:02";
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

    bool createV2Ok = true;
    Poco::Net::HTTPResponse::HTTPStatus createV2Status = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Object::Ptr createV2Response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());

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
    int createV2Calls = 0;
    int deleteCalls = 0;
    std::string lastSubscriberId;
    std::string lastOperatorId;
    std::string lastGroupId;
    std::string lastObjectType;
    bool lastConfigRawRequired = false;
    OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse lastSuccessResponse =
        OpenWifi::RESTAPI::ParentalControl::MutationSuccessResponse::Ok;
    std::string lastClientMac;
    std::vector<std::string> lastClientMacs;
    std::string lastGatewaySerial;
};

DeviceHandlerState g_state;

void ResetState() {
    g_state = DeviceHandlerState{};
    g_state.getListArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    g_state.getListError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.createResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.createV2Response = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.getSingleResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.deleteResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g_state.extractedConfigRaw = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
}

class TestGroupDevicesListHandler final : public OpenWifi::RESTAPI_group_devices_list_handler {
  public:
    using OpenWifi::RESTAPI_group_devices_list_handler::RESTAPI_group_devices_list_handler;

    void setParsedBody(const Poco::JSON::Object::Ptr &body) { ParsedBody_ = body; }
};

class TestGroupDevicesListV2Handler final : public OpenWifi::RESTAPI_group_devices_list_v2_handler {
  public:
    using OpenWifi::RESTAPI_group_devices_list_v2_handler::RESTAPI_group_devices_list_v2_handler;

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

bool CreateGroupDevicesV2(RESTAPIHandler *, const std::string &subscriberId, const std::string &groupId,
                         const Poco::JSON::Object &body, Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                         Poco::JSON::Object::Ptr &callResponse) {
    ++g_state.createV2Calls;
    g_state.lastSubscriberId = subscriberId;
    g_state.lastGroupId = groupId;
    g_state.lastClientMacs.clear();
    if (body.has("client_macs") && body.isArray("client_macs")) {
        auto arr = body.getArray("client_macs");
        if (arr) {
            for (std::size_t i = 0; i < arr->size(); ++i) {
                g_state.lastClientMacs.push_back(arr->getElement<std::string>(i));
            }
        }
    }
    callStatus = g_state.createV2Status;
    callResponse = g_state.createV2Response;
    return g_state.createV2Ok;
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
#include "../../src/RESTAPI/RESTAPI_group_devices_list_v2_handler.cpp"
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

void TestV2PostSuccessSingleDevice() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto devicesArr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    auto dev = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    dev->set("client_mac", "aa:bb:cc:dd:ee:ff");
    devicesArr->add(dev);
    responseObject->set("devices", devicesArr);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createV2Response = responseObject;

    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"AA:BB:CC:DD:EE:FF\"]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(kValidMac);
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            ExpectEq(g_state.createV2Calls, 1, "Exactly one V2 downstream call should be made");
            ExpectEq(g_state.createCalls, 0, "V1 downstream call must not be made for V2");
            ExpectEq(g_state.lastClientMacs.size(), static_cast<std::size_t>(1), "Should pass one MAC to V2 downstream");
            ExpectEq(g_state.lastClientMacs[0], std::string(kValidMac), "MAC should match normalized colon format");
            auto parsed = ParseObject(response.body());
            Expect(parsed->has("devices"), "Response must have devices array");
            Expect(!parsed->has("config-raw"), "Response must strip config-raw");
        }
    );
}

void TestV2PostSuccessMultipleDevices() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto devicesArr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    devicesArr->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    devicesArr->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    responseObject->set("devices", devicesArr);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createV2Response = responseObject;

    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"AA:BB:CC:DD:EE:FF\",\"AA:BB:CC:DD:EE:02\"]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(kValidMac);
            arr->add(kValidMac2);
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            ExpectEq(g_state.createV2Calls, 1, "Bulk request must result in exactly ONE downstream V2 call");
            ExpectEq(g_state.createCalls, 0, "V1 downstream call must not be made");
            ExpectEq(g_state.lastClientMacs.size(), static_cast<std::size_t>(2), "Should pass all MACs in single downstream call");
            ExpectEq(g_state.lastClientMacs[0], std::string(kValidMac), "First MAC normalized");
            ExpectEq(g_state.lastClientMacs[1], std::string(kValidMac2), "Second MAC normalized");
            auto parsed = ParseObject(response.body());
            Expect(parsed->has("devices"), "Response must have devices array");
        }
    );
}

void TestV2PostRejectsStringClientMacs() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":\"AA:BB:CC:DD:EE:FF\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_macs", kValidMac); // String instead of array
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call when client_macs is string");
        }
    );
}

void TestV2PostRejectsLegacyClientMac() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_mac\":\"AA:BB:CC:DD:EE:FF\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_mac", kValidMac); // Legacy V1 field
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call when legacy client_mac is provided to V2");
        }
    );
}

void TestV2PostRejectsEmptyArray() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("client_macs", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call for empty client_macs array");
        }
    );
}

void TestV2PostPropagatesConflict() {
    g_state.createV2Ok = false;
    g_state.createV2Status = Poco::Net::HTTPResponse::HTTP_CONFLICT;
    auto errObj = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    errObj->set("error", "conflict");
    errObj->set("message", "device_already_assigned");
    g_state.createV2Response = errObj;

    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"AA:BB:CC:DD:EE:FF\"]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_CONFLICT,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(kValidMac);
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 1, "Downstream call made");
        }
    );
}

void TestV2PostRejectsMalformedMac() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"not-a-valid-mac\"]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(std::string("not-a-valid-mac"));
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call for malformed MAC in client_macs");
        }
    );
}

void TestV2PostRejectsExceedingMaxMacs() {
    std::string rawJson = "{\"client_macs\":[";
    for (int i = 0; i < 101; ++i) {
        if (i > 0) {
            rawJson += ",";
        }
        rawJson += "\"00:11:22:33:44:55\"";
    }
    rawJson += "]}";

    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        rawJson,
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            for (int i = 0; i < 101; ++i) {
                arr->add(std::string("00:11:22:33:44:55"));
            }
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call when client_macs exceeds 100");
        }
    );
}

void TestV2PostDeduplicatesMacs() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    auto devicesArr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    devicesArr->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    devicesArr->add(Poco::JSON::Object::Ptr(new Poco::JSON::Object()));
    responseObject->set("devices", devicesArr);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createV2Response = responseObject;

    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"AA:BB:CC:DD:EE:FF\",\"aa-bb-cc-dd-ee-ff\",\"aabbccddeeff\",\"00:11:22:33:44:55\"]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(std::string("AA:BB:CC:DD:EE:FF")); // Colon format
            arr->add(std::string("aa-bb-cc-dd-ee-ff")); // Hyphen format
            arr->add(std::string("aabbccddeeff"));       // 12-digit hex format
            arr->add(std::string("00:11:22:33:44:55")); // Distinct MAC
            body->set("client_macs", arr);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            ExpectEq(g_state.createV2Calls, 1, "Bulk request should execute single downstream call");
            ExpectEq(g_state.lastClientMacs.size(), static_cast<std::size_t>(2), "3 duplicate MAC representations should be deduplicated to 2 total entries");
            ExpectEq(g_state.lastClientMacs[0], kValidMac, "Equivalent colon, hyphen, and hex MACs collapse to canonical form");
            ExpectEq(g_state.lastClientMacs[1], std::string("00:11:22:33:44:55"), "Second distinct MAC canonicalized");
            auto parsed = ParseObject(response.body());
            Expect(parsed->has("devices"), "Response must have devices array");
        }
    );
}

void TestV2PostRejectsUnknownField() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{\"client_macs\":[\"AA:BB:CC:DD:EE:FF\"],\"foo\":\"bar\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            auto arr = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            arr->add(kValidMac);
            body->set("client_macs", arr);
            body->set("foo", std::string("bar"));
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call when unknown field is present");
        }
    );
}

void TestV2PostRejectsMissingClientMacs() {
    RunHandlerRequest<TestGroupDevicesListV2Handler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v2/groups/x/devices",
        "{}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupDevicesListV2Handler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            handler.setParsedBody(body);
        },
        [](const FakeResponse &) {
            ExpectEq(g_state.createV2Calls, 0, "No downstream call when client_macs is missing");
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
    {"V2PostSuccessSingleDevice", TestV2PostSuccessSingleDevice},
    {"V2PostSuccessMultipleDevices", TestV2PostSuccessMultipleDevices},
    {"V2PostRejectsStringClientMacs", TestV2PostRejectsStringClientMacs},
    {"V2PostRejectsLegacyClientMac", TestV2PostRejectsLegacyClientMac},
    {"V2PostRejectsEmptyArray", TestV2PostRejectsEmptyArray},
    {"V2PostRejectsMalformedMac", TestV2PostRejectsMalformedMac},
    {"V2PostRejectsExceedingMaxMacs", TestV2PostRejectsExceedingMaxMacs},
    {"V2PostPropagatesConflict", TestV2PostPropagatesConflict},
    {"V2PostDeduplicatesMacs", TestV2PostDeduplicatesMacs},
    {"V2PostRejectsUnknownField", TestV2PostRejectsUnknownField},
    {"V2PostRejectsMissingClientMacs", TestV2PostRejectsMissingClientMacs},
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
