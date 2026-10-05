/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_schedule_groups_list_handler.h"

namespace {

const std::string kValidScheduleId = "22222222-2222-4222-8222-222222222222";
const std::string kValidSubscriberId = "11111111-1111-4111-8111-111111111111";

struct ScheduleGroupsHandlerState {
    bool getScheduleGroupsOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getScheduleGroupsStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Array::Ptr getScheduleGroupsArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr getScheduleGroupsError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string lastSubscriberId;
    std::string lastScheduleId;
    int getScheduleGroupsCallCount = 0;
};

ScheduleGroupsHandlerState g_state;

void ResetState() {
    g_state = ScheduleGroupsHandlerState{};
    g_state.getScheduleGroupsArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    g_state.getScheduleGroupsError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
}

class TestScheduleGroupsListHandler final : public OpenWifi::RESTAPI_schedule_groups_list_handler {
  public:
    using OpenWifi::RESTAPI_schedule_groups_list_handler::RESTAPI_schedule_groups_list_handler;
};

} // namespace

namespace OpenWifi::RESTAPI::ParentalControl {

void ForwardParentalControlErrorResponse(RESTAPIHandler *handler,
                                        Poco::Net::HTTPResponse::HTTPStatus status,
                                        const Poco::JSON::Object::Ptr &downstreamResponse) {
    if (handler != nullptr) {
        handler->ForwardErrorResponse(handler, status, downstreamResponse);
    }
}

} // namespace OpenWifi::RESTAPI::ParentalControl

namespace OpenWifi::SDK::ParentalControl {

bool GetScheduleGroups(RESTAPIHandler *, const std::string &subscriberId,
                       const std::string &scheduleId,
                       Poco::Net::HTTPResponse::HTTPStatus &callStatus,
                       Poco::JSON::Array::Ptr &arrayResponse,
                       Poco::JSON::Object::Ptr &objectResponse) {
    g_state.lastSubscriberId = subscriberId;
    g_state.lastScheduleId = scheduleId;
    g_state.getScheduleGroupsCallCount++;
    callStatus = g_state.getScheduleGroupsStatus;
    arrayResponse = g_state.getScheduleGroupsArray;
    objectResponse = g_state.getScheduleGroupsError;
    return g_state.getScheduleGroupsOk;
}

} // namespace OpenWifi::SDK::ParentalControl

#include "../../src/RESTAPI/RESTAPI_schedule_groups_list_handler.cpp"

namespace {

void TestScheduleGroupsSuccessMixedDeviceCounts() {
    auto g1 = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g1->set("id", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa");
    g1->set("subscriber_id", kValidSubscriberId);
    g1->set("group_config_index", 1);
    g1->set("name", "Kids");
    g1->set("description", "Tablets");
    g1->set("created_at", "2026-06-15T12:00:00Z");
    g1->set("updated_at", "2026-06-15T12:30:00Z");
    g1->set("device_count", 3);
    g_state.getScheduleGroupsArray->add(g1);

    auto g2 = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g2->set("id", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    g2->set("subscriber_id", kValidSubscriberId);
    g2->set("group_config_index", 2);
    g2->set("name", "Guests");
    g2->set("description", Poco::Dynamic::Var());
    g2->set("created_at", "2026-06-15T12:00:00Z");
    g2->set("updated_at", "2026-06-15T12:30:00Z");
    g2->set("device_count", 0);
    g_state.getScheduleGroupsArray->add(g2);

    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", kValidScheduleId}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 1, "SDK called exactly once");
            ExpectEq(g_state.lastSubscriberId, kValidSubscriberId, "subscriber ID matches");
            ExpectEq(g_state.lastScheduleId, kValidScheduleId, "schedule ID matches");

            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 2, "array size matches");

            auto item0 = array->getObject(0);
            ExpectEq(item0->getValue<std::string>("id"), std::string("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"), "g1 id");
            ExpectEq(item0->getValue<std::string>("name"), std::string("Kids"), "g1 name");
            ExpectEq(item0->getValue<int>("device_count"), 3, "g1 device_count");
            ExpectEq(item0->getValue<int>("group_config_index"), 1, "g1 group_config_index");
            ExpectEq(item0->getValue<std::string>("description"), std::string("Tablets"), "g1 description");
            Expect(!item0->has("client_mac"), "g1 must not expose client_mac");
            Expect(!item0->has("client_macs"), "g1 must not expose client_macs");

            auto item1 = array->getObject(1);
            ExpectEq(item1->getValue<std::string>("id"), std::string("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"), "g2 id");
            ExpectEq(item1->getValue<std::string>("name"), std::string("Guests"), "g2 name");
            ExpectEq(item1->getValue<int>("device_count"), 0, "g2 device_count");
            ExpectEq(item1->getValue<int>("group_config_index"), 2, "g2 group_config_index");
            Expect(item1->isNull("description"), "g2 description is null");
            Expect(!item1->has("client_mac"), "g2 must not expose client_mac");
            Expect(!item1->has("client_macs"), "g2 must not expose client_macs");
        }
    );
}

void TestScheduleGroupsSuccessEmptyList() {
    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", kValidScheduleId}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 1, "SDK called once");
            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 0, "array is empty");
            ExpectEq(response.body(), std::string("[]"), "body is empty JSON array");
        }
    );
}

void TestScheduleGroupsRejectsMissingSubscriberId() {
    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", kValidScheduleId}},
        "",
        "",
        Poco::Net::HTTPResponse::HTTP_FORBIDDEN,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 0, "SDK must not be called when subscriber is missing");
        }
    );
}

void TestScheduleGroupsRejectsEmptyScheduleId() {
    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", ""}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 0, "SDK must not be called when schedule_id is empty");
        }
    );
}

void TestScheduleGroupsRejectsMalformedScheduleId() {
    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", "not-a-valid-uuid"}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_REQUEST,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 0, "SDK must not be called when schedule_id is malformed");
        }
    );
}

void TestScheduleGroupsScheduleNotFound() {
    g_state.getScheduleGroupsOk = false;
    g_state.getScheduleGroupsStatus = Poco::Net::HTTPResponse::HTTP_NOT_FOUND;
    auto errObj = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    errObj->set("code", "schedule_not_found");
    errObj->set("message", "Schedule not found");
    g_state.getScheduleGroupsError->set("error", errObj);

    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", kValidScheduleId}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_NOT_FOUND,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 1, "SDK called");
        }
    );
}

void TestScheduleGroupsForwardsDownstreamFailure() {
    g_state.getScheduleGroupsOk = false;
    g_state.getScheduleGroupsStatus = Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY;
    g_state.getScheduleGroupsError->set("error", "storage_failure");

    RunHandlerRequest<TestScheduleGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/schedules/x/groups",
        "",
        {{"schedule_id", kValidScheduleId}},
        kValidSubscriberId,
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY,
        nullptr,
        [](const FakeResponse &) {
            ExpectEq(g_state.getScheduleGroupsCallCount, 1, "SDK called");
        }
    );
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"ScheduleGroupsSuccessMixedDeviceCounts", TestScheduleGroupsSuccessMixedDeviceCounts},
    {"ScheduleGroupsSuccessEmptyList", TestScheduleGroupsSuccessEmptyList},
    {"ScheduleGroupsRejectsMissingSubscriberId", TestScheduleGroupsRejectsMissingSubscriberId},
    {"ScheduleGroupsRejectsEmptyScheduleId", TestScheduleGroupsRejectsEmptyScheduleId},
    {"ScheduleGroupsRejectsMalformedScheduleId", TestScheduleGroupsRejectsMalformedScheduleId},
    {"ScheduleGroupsScheduleNotFound", TestScheduleGroupsScheduleNotFound},
    {"ScheduleGroupsForwardsDownstreamFailure", TestScheduleGroupsForwardsDownstreamFailure},
};

} // namespace

int main() {
    int failures = 0;
    for (const auto &test : kTests) {
        try {
            ResetState();
            test.second();
            std::cout << "PASS: " << test.first << "\n";
        } catch (const std::exception &e) {
            failures++;
            std::cerr << "FAIL: " << test.first << " - " << e.what() << "\n";
        }
    }
    return failures != 0 ? 1 : 0;
}
