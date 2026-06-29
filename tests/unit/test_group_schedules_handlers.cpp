/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_group_schedules_handler.h"
#include "RESTAPI/RESTAPI_group_schedules_list_handler.h"

namespace {

const std::string kValidGroupId = "11111111-1111-4111-8111-111111111111";
const std::string kValidScheduleId = "22222222-2222-4222-8222-222222222222";
const std::string kAnotherScheduleId = "33333333-3333-4333-8333-333333333333";
const std::string kInvalidUuid = "not-a-uuid";

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
    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/schedules",
        "",
        {{"group_id", kInvalidUuid}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST
    );
}

void TestListGetReturnsJsonArrayOnSuccess() {
    auto item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("schedule_id", kValidScheduleId);
    g_state.getListArray->add(item);

    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/schedules",
        "",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(array->size(), static_cast<std::size_t>(1), "GET schedules should return one entry");
            ExpectEq(g_state.lastGroupId, kValidGroupId, "group id should be forwarded");
        }
    );
}

void TestPostRejectsInvalidScheduleId() {
    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v1/groups/x/schedules",
        "{\"schedule_id\":\"bad\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupSchedulesListHandler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("schedule_id", kInvalidUuid);
            handler.setParsedBody(body);
        }
    );
}

void TestPostStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("schedule_id", kValidScheduleId);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.createResponse = responseObject;

    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_POST,
        "/api/v1/groups/x/schedules",
        "{\"schedule_id\":\"ok\"}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupSchedulesListHandler &handler) {
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("schedule_id", kValidScheduleId);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            auto parsed = ParseObject(response.body());
            Expect(!parsed->has("config-raw"), "POST response should strip config-raw");
            ExpectEq(parsed->getValue<std::string>("schedule_id"), kValidScheduleId, "schedule_id should remain");
        }
    );
}

void TestPutRejectsDuplicateScheduleIds() {
    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_PUT,
        "/api/v1/groups/x/schedules",
        "{\"schedule_ids\":[]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupSchedulesListHandler &handler) {
            auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            ids->add(kValidScheduleId);
            ids->add(kValidScheduleId);
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("schedule_ids", ids);
            handler.setParsedBody(body);
        }
    );
}

void TestPutRejectsNonStringScheduleIdEntry() {
    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_PUT,
        "/api/v1/groups/x/schedules",
        "{\"schedule_ids\":[]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        [](TestGroupSchedulesListHandler &handler) {
            auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            ids->add(7);
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("schedule_ids", ids);
            handler.setParsedBody(body);
        }
    );
}

void TestPutStripsConfigRawAndReturnsObject() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("replaced", true);
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.replaceResponse = responseObject;

    RunHandlerRequest<TestGroupSchedulesListHandler>(
        Poco::Net::HTTPRequest::HTTP_PUT,
        "/api/v1/groups/x/schedules",
        "{\"schedule_ids\":[]}",
        {{"group_id", kValidGroupId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK,
        [](TestGroupSchedulesListHandler &handler) {
            auto ids = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
            ids->add(kValidScheduleId);
            ids->add(kAnotherScheduleId);
            auto body = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
            body->set("schedule_ids", ids);
            handler.setParsedBody(body);
        },
        [](const FakeResponse &response) {
            auto parsed = ParseObject(response.body());
            Expect(!parsed->has("config-raw"), "PUT response should strip config-raw");
            ExpectEq(g_state.replaceCount, static_cast<std::size_t>(2), "two schedule ids should be forwarded");
        }
    );
}

void TestSingleGetReturnsObjectOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("schedule_id", kValidScheduleId);
    g_state.getSingleResponse = responseObject;

    RunHandlerRequest<TestGroupSchedulesHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups/x/schedules/y",
        "",
        {{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto parsed = ParseObject(response.body());
            ExpectEq(parsed->getValue<std::string>("schedule_id"), kValidScheduleId, "GET should return schedule object");
        }
    );
}

void TestSingleDeleteRejectsMissingOwner() {
    RunHandlerRequest<TestGroupSchedulesHandler>(
        Poco::Net::HTTPRequest::HTTP_DELETE,
        "/api/v1/groups/x/schedules/y",
        "",
        {{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_FORBIDDEN
    );
}

void TestSingleDeleteReturnsOkOnSuccess() {
    auto responseObject = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    responseObject->set("config-raw", Poco::JSON::Array::Ptr(new Poco::JSON::Array()));
    g_state.deleteResponse = responseObject;

    RunHandlerRequest<TestGroupSchedulesHandler>(
        Poco::Net::HTTPRequest::HTTP_DELETE,
        "/api/v1/groups/x/schedules/y",
        "",
        {{"group_id", kValidGroupId}, {"schedule_id", kValidScheduleId}},
        "subscriber-1",
        "operator-1",
        Poco::Net::HTTPResponse::HTTP_OK
    );
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

