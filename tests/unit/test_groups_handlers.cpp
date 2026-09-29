/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_groups_list_handler.h"

namespace {

struct GroupsHandlerState {
    bool getGroupsOk = true;
    Poco::Net::HTTPResponse::HTTPStatus getGroupsStatus = Poco::Net::HTTPResponse::HTTP_OK;
    Poco::JSON::Array::Ptr getGroupsArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr getGroupsError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    std::string lastSubscriberId;
};

GroupsHandlerState g_state;

void ResetState() {
    g_state = GroupsHandlerState{};
    g_state.getGroupsArray = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    g_state.getGroupsError = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
}

class TestGroupsListHandler final : public OpenWifi::RESTAPI_groups_list_handler {
  public:
    using OpenWifi::RESTAPI_groups_list_handler::RESTAPI_groups_list_handler;
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

bool GetGroups(RESTAPIHandler *, const std::string &subscriberId,
               Poco::Net::HTTPResponse::HTTPStatus &callStatus,
               Poco::JSON::Array::Ptr &arrayResponse,
               Poco::JSON::Object::Ptr &objectResponse) {
    g_state.lastSubscriberId = subscriberId;
    callStatus = g_state.getGroupsStatus;
    arrayResponse = g_state.getGroupsArray;
    objectResponse = g_state.getGroupsError;
    return g_state.getGroupsOk;
}

bool CreateGroup(RESTAPIHandler *, const std::string &,
                 const Poco::JSON::Object &,
                 Poco::Net::HTTPResponse::HTTPStatus &,
                 Poco::JSON::Object::Ptr &) {
    return false;
}

bool GetGroup(RESTAPIHandler *, const std::string &,
              const std::string &,
              Poco::Net::HTTPResponse::HTTPStatus &,
              Poco::JSON::Object::Ptr &) {
    return false;
}

bool UpdateGroup(RESTAPIHandler *, const std::string &,
                 const std::string &, const Poco::JSON::Object &,
                 Poco::Net::HTTPResponse::HTTPStatus &,
                 Poco::JSON::Object::Ptr &) {
    return false;
}

bool DeleteGroup(RESTAPIHandler *, const std::string &,
                 const std::string &,
                 Poco::Net::HTTPResponse::HTTPStatus &,
                 Poco::JSON::Object::Ptr &,
                 std::string &) {
    return false;
}

} // namespace OpenWifi::SDK::ParentalControl

#include "../../src/RESTAPI/RESTAPI_groups_list_handler.cpp"

namespace {

void TestListGroupsPreservesPositiveDeviceCount() {
    auto group = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    group->set("id", "c1f7b0f6-7b24-4f51-b0e6-996bb31b6fa2");
    group->set("name", "Kids");
    group->set("device_count", 3);
    g_state.getGroupsArray->add(group);

    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 1, "array size");
            auto item = array->getObject(0);
            Expect(item->has("device_count"), "item has device_count");
            ExpectEq(item->getValue<int>("device_count"), 3, "device_count matches 3");
            ExpectEq(item->getValue<std::string>("name"), std::string("Kids"), "group name preserved");
        }
    );
}

void TestListGroupsPreservesZeroDeviceCount() {
    auto group = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    group->set("id", "e4f8b2d1-4a11-4c77-90c1-228bb99b5aa1");
    group->set("name", "Empty Group");
    group->set("device_count", 0);
    g_state.getGroupsArray->add(group);

    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 1, "array size");
            auto item = array->getObject(0);
            Expect(item->has("device_count"), "item has device_count");
            ExpectEq(item->getValue<int>("device_count"), 0, "device_count matches 0");
            ExpectEq(item->getValue<std::string>("name"), std::string("Empty Group"), "group name preserved");
        }
    );
}

void TestListGroupsPreservesMultiGroupDeviceCountsAndOrdering() {
    auto g1 = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g1->set("id", "11111111-1111-4111-8111-111111111111");
    g1->set("name", "Group One");
    g1->set("device_count", 3);
    g_state.getGroupsArray->add(g1);

    auto g2 = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g2->set("id", "22222222-2222-4222-8222-222222222222");
    g2->set("name", "Group Two");
    g2->set("device_count", 0);
    g_state.getGroupsArray->add(g2);

    auto g3 = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    g3->set("id", "33333333-3333-4333-8333-333333333333");
    g3->set("name", "Group Three");
    g3->set("device_count", 5);
    g_state.getGroupsArray->add(g3);

    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 3, "array size");

            auto item0 = array->getObject(0);
            ExpectEq(item0->getValue<std::string>("id"), std::string("11111111-1111-4111-8111-111111111111"), "g1 id");
            ExpectEq(item0->getValue<int>("device_count"), 3, "g1 device_count");

            auto item1 = array->getObject(1);
            ExpectEq(item1->getValue<std::string>("id"), std::string("22222222-2222-4222-8222-222222222222"), "g2 id");
            ExpectEq(item1->getValue<int>("device_count"), 0, "g2 device_count");

            auto item2 = array->getObject(2);
            ExpectEq(item2->getValue<std::string>("id"), std::string("33333333-3333-4333-8333-333333333333"), "g3 id");
            ExpectEq(item2->getValue<int>("device_count"), 5, "g3 device_count");
        }
    );
}

void TestListGroupsPreservesAllStandardGroupFields() {
    auto group = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    group->set("id", "c1f7b0f6-7b24-4f51-b0e6-996bb31b6fa2");
    group->set("subscriber_id", "11111111-1111-4111-8111-111111111111");
    group->set("group_config_index", 1);
    group->set("name", "Full Group");
    group->set("description", "Detailed description");
    group->set("created_at", "2026-06-15T12:00:00Z");
    group->set("updated_at", "2026-06-15T12:30:00Z");
    group->set("device_count", 4);
    g_state.getGroupsArray->add(group);

    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "11111111-1111-4111-8111-111111111111",
        "",
        Poco::Net::HTTPResponse::HTTP_OK,
        nullptr,
        [](const FakeResponse &response) {
            auto array = ParseArray(response.body());
            ExpectEq(static_cast<int>(array->size()), 1, "array size");
            auto item = array->getObject(0);
            ExpectEq(item->getValue<std::string>("id"), std::string("c1f7b0f6-7b24-4f51-b0e6-996bb31b6fa2"), "id");
            ExpectEq(item->getValue<std::string>("subscriber_id"), std::string("11111111-1111-4111-8111-111111111111"), "subscriber_id");
            ExpectEq(item->getValue<int>("group_config_index"), 1, "group_config_index");
            ExpectEq(item->getValue<std::string>("name"), std::string("Full Group"), "name");
            ExpectEq(item->getValue<std::string>("description"), std::string("Detailed description"), "description");
            ExpectEq(item->getValue<std::string>("created_at"), std::string("2026-06-15T12:00:00Z"), "created_at");
            ExpectEq(item->getValue<std::string>("updated_at"), std::string("2026-06-15T12:30:00Z"), "updated_at");
            ExpectEq(item->getValue<int>("device_count"), 4, "device_count");
        }
    );
}

void TestListGroupsRejectsMissingSubscriberId() {
    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "",
        "",
        Poco::Net::HTTPResponse::HTTP_FORBIDDEN
    );
}

void TestListGroupsForwardsDownstreamErrors() {
    g_state.getGroupsOk = false;
    g_state.getGroupsStatus = Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY;
    g_state.getGroupsError->set("error", "mango_error");
    g_state.getGroupsError->set("message", "downstream failure");

    RunHandlerRequest<TestGroupsListHandler>(
        Poco::Net::HTTPRequest::HTTP_GET,
        "/api/v1/groups",
        "",
        {},
        "subscriber-1",
        "",
        Poco::Net::HTTPResponse::HTTP_BAD_GATEWAY,
        nullptr,
        [](const FakeResponse &response) {
            auto obj = ParseObject(response.body());
            Expect(obj->has("error"), "error field present");
        }
    );
}

const std::vector<std::pair<std::string, std::function<void()>>> kTests = {
    {"ListGroupsPreservesPositiveDeviceCount", TestListGroupsPreservesPositiveDeviceCount},
    {"ListGroupsPreservesZeroDeviceCount", TestListGroupsPreservesZeroDeviceCount},
    {"ListGroupsPreservesMultiGroupDeviceCountsAndOrdering", TestListGroupsPreservesMultiGroupDeviceCountsAndOrdering},
    {"ListGroupsPreservesAllStandardGroupFields", TestListGroupsPreservesAllStandardGroupFields},
    {"ListGroupsRejectsMissingSubscriberId", TestListGroupsRejectsMissingSubscriberId},
    {"ListGroupsForwardsDownstreamErrors", TestListGroupsForwardsDownstreamErrors},
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
