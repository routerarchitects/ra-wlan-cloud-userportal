/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "test_parental_control_test_helpers.h"
#include "RESTAPI/RESTAPI_subscriber_location_handler.h"

namespace {

const std::string kSubscriber  = "subscriber-1";
const std::string kVenueId     = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
const std::string kLocationId  = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
const std::string kGoodTz      = "UTC";
const std::string kBadTz       = "Not/A_Timezone";

struct State {
    bool     getVenuesOk      = true;
    bool     getLocOk         = true;
    bool     createLocOk      = true;
    bool     putLocOk         = true;
    bool     clearLocOk       = true;
    bool     deleteLocOk      = true;
    std::string venueLocId;                        // empty = no location linked
    Poco::JSON::Object::Ptr locResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    int createCalls = 0, putCalls = 0, clearCalls = 0, deleteCalls = 0;
    std::string capturedTz, capturedVenueId, capturedLocId;
} g;

void Reset() { g = State{}; g.locResponse = Poco::JSON::Object::Ptr(new Poco::JSON::Object()); }

class Handler final : public OpenWifi::RESTAPI_subscriber_location_handler {
  public:
    using OpenWifi::RESTAPI_subscriber_location_handler::RESTAPI_subscriber_location_handler;
    void setBody(const Poco::JSON::Object::Ptr &b) { ParsedBody_ = b; }
};

Poco::JSON::Object::Ptr Body(std::initializer_list<std::pair<std::string,std::string>> kv) {
    auto o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    for (auto &p : kv) o->set(p.first, p.second);
    return o;
}

} // namespace

namespace OpenWifi::SDK::Prov::Venue {
bool GetVenues(RESTAPIHandler *, const std::string &, ProvObjects::VenueList &out,
               Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &) {
    s = Poco::Net::HTTPResponse::HTTP_OK;
    if (!g.getVenuesOk) { s = Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR; return false; }
    ProvObjects::Venue v; v.info.id = kVenueId; v.location = g.venueLocId;
    out.venues.push_back(v);
    return true;
}
bool Get(RESTAPIHandler *, const std::string &, ProvObjects::Venue &,
         Poco::Net::HTTPServerResponse::HTTPStatus &, Poco::JSON::Object::Ptr &) { return false; }
bool CreateLocation(RESTAPIHandler *, const std::string &venueId,
                    const Poco::JSON::Object::Ptr &d,
                    Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &r) {
    ++g.createCalls; g.capturedVenueId = venueId;
    if (d && d->has("timezone")) g.capturedTz = d->getValue<std::string>("timezone");
    s = Poco::Net::HTTPResponse::HTTP_OK; r = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    return g.createLocOk;
}
bool ClearLocation(RESTAPIHandler *, const std::string &venueId,
                   Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &) {
    ++g.clearCalls; g.capturedVenueId = venueId;
    s = g.clearLocOk ? Poco::Net::HTTPResponse::HTTP_OK : Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    return g.clearLocOk;
}
} // namespace OpenWifi::SDK::Prov::Venue

namespace OpenWifi::SDK::Prov::Location {
bool Get(RESTAPIHandler *, const std::string &lid, ProvObjects::Location &,
         Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &r) {
    g.capturedLocId = lid; s = Poco::Net::HTTPResponse::HTTP_OK; r = g.locResponse;
    return g.getLocOk;
}
bool Put(RESTAPIHandler *, const std::string &lid, const Poco::JSON::Object::Ptr &b,
         Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &r) {
    ++g.putCalls; g.capturedLocId = lid;
    if (b && b->has("timezone")) g.capturedTz = b->getValue<std::string>("timezone");
    s = Poco::Net::HTTPResponse::HTTP_OK; r = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    return g.putLocOk;
}
bool Delete(RESTAPIHandler *, const std::string &lid,
            Poco::Net::HTTPServerResponse::HTTPStatus &s, Poco::JSON::Object::Ptr &) {
    ++g.deleteCalls; g.capturedLocId = lid;
    s = g.deleteLocOk ? Poco::Net::HTTPResponse::HTTP_OK : Poco::Net::HTTPResponse::HTTP_INTERNAL_SERVER_ERROR;
    return g.deleteLocOk;
}
} // namespace OpenWifi::SDK::Prov::Location

#include "../../src/RESTAPI/RESTAPI_subscriber_location_handler.cpp"

namespace {

using HTTP = Poco::Net::HTTPResponse;

void Run(const std::string &method, const std::string &sub,
         HTTP::HTTPStatus expected,
         std::function<void(Handler&)> setup = nullptr,
         std::function<void(const FakeResponse&)> check = nullptr) {
    RunHandlerRequest<Handler>(method, "/api/v1/subscriber/location", "",
                               {}, sub, "", expected, setup, check);
}

// ---- GET ------------------------------------------------------------------
void TestGetNoAuth()       { Run("GET","",    HTTP::HTTP_UNAUTHORIZED); }
void TestGetVenuesFailure() { g.getVenuesOk=false; Run("GET",kSubscriber, HTTP::HTTP_INTERNAL_SERVER_ERROR); }
void TestGetNoLocation()   { Run("GET",kSubscriber, HTTP::HTTP_NOT_FOUND); }
void TestGetOk() {
    g.venueLocId = kLocationId;
    g.locResponse->set("timezone", kGoodTz);
    Run("GET", kSubscriber, HTTP::HTTP_OK, nullptr, [](const FakeResponse &r){
        ExpectEq(ParseObject(r.body())->getValue<std::string>("timezone"), kGoodTz, "GET tz");
        ExpectEq(g.capturedLocId, kLocationId, "GET loc id");
    });
}

// ---- POST -----------------------------------------------------------------
void TestPostNoAuth()    { Run("POST","", HTTP::HTTP_UNAUTHORIZED); }
void TestPostNoTz()      { Run("POST",kSubscriber, HTTP::HTTP_BAD_REQUEST,
    [](Handler &h){ h.setBody(Body({{"name","Home"}})); },
    [](const FakeResponse&){ ExpectEq(g.createCalls,0,"no create"); }); }
void TestPostBadTz()     { Run("POST",kSubscriber, HTTP::HTTP_BAD_REQUEST,
    [](Handler &h){ h.setBody(Body({{"timezone",kBadTz}})); },
    [](const FakeResponse&){ ExpectEq(g.createCalls,0,"no create"); }); }
void TestPostDuplicate() { g.venueLocId=kLocationId; Run("POST",kSubscriber, HTTP::HTTP_BAD_REQUEST,
    [](Handler &h){ h.setBody(Body({{"timezone",kGoodTz}})); },
    [](const FakeResponse&){ ExpectEq(g.createCalls,0,"no create when existing"); }); }
void TestPostUnallowedField() {
    for (const auto &f : {"id", "futurePrivilegedField"}) {
        Reset();
        Run("POST",kSubscriber, HTTP::HTTP_BAD_REQUEST,
            [&f](Handler &h){ h.setBody(Body({{"timezone",kGoodTz},{f,"custom-val"}})); },
            [](const FakeResponse&){ ExpectEq(g.createCalls,0,"no create for unallowed field"); });
    }
}
void TestPostOk() { Run("POST",kSubscriber, HTTP::HTTP_OK,
    [](Handler &h){ h.setBody(Body({{"timezone",kGoodTz},{"name","Home"}})); },
    [](const FakeResponse&){
        ExpectEq(g.createCalls,1,"create called once");
        ExpectEq(g.capturedVenueId,kVenueId,"venue id");
        ExpectEq(g.capturedTz,kGoodTz,"tz forwarded"); }); }

// ---- PUT ------------------------------------------------------------------
void TestPutNoAuth()    { Run("PUT","", HTTP::HTTP_UNAUTHORIZED); }
void TestPutEmptyBody() { g.venueLocId=kLocationId; Run("PUT",kSubscriber, HTTP::HTTP_BAD_REQUEST,
    [](Handler &h){ h.setBody(Poco::JSON::Object::Ptr(new Poco::JSON::Object())); },
    [](const FakeResponse&){ ExpectEq(g.putCalls,0,"no put"); }); }
void TestPutForbiddenFields() {
    for (const auto &f : {"entity","inUse","managementPolicy","id","created","modified","notes","futurePrivilegedField"}) {
        Reset(); g.venueLocId=kLocationId;
        Run("PUT",kSubscriber, HTTP::HTTP_BAD_REQUEST,
            [&f](Handler &h){ h.setBody(Body({{f,"v"}})); },
            [](const FakeResponse&){ ExpectEq(g.putCalls,0,"no put for forbidden field"); });
    }
}
void TestPutBadTz()     { g.venueLocId=kLocationId; Run("PUT",kSubscriber, HTTP::HTTP_BAD_REQUEST,
    [](Handler &h){ h.setBody(Body({{"timezone",kBadTz}})); },
    [](const FakeResponse&){ ExpectEq(g.putCalls,0,"no put"); }); }
void TestPutNoLocation(){ Run("PUT",kSubscriber, HTTP::HTTP_NOT_FOUND,
    [](Handler &h){ h.setBody(Body({{"name","x"}})); }); }
void TestPutOk()        { g.venueLocId=kLocationId; Run("PUT",kSubscriber, HTTP::HTTP_OK,
    [](Handler &h){ h.setBody(Body({{"timezone",kGoodTz}})); },
    [](const FakeResponse&){
        ExpectEq(g.putCalls,1,"put called"); ExpectEq(g.capturedLocId,kLocationId,"loc id");
        ExpectEq(g.capturedTz,kGoodTz,"tz forwarded"); }); }

// ---- DELETE ---------------------------------------------------------------
void TestDeleteNoAuth()    { Run("DELETE","", HTTP::HTTP_UNAUTHORIZED); }
void TestDeleteNoLocation(){ Run("DELETE",kSubscriber, HTTP::HTTP_NOT_FOUND, nullptr,
    [](const FakeResponse&){ ExpectEq(g.clearCalls+g.deleteCalls,0,"no sdk calls"); }); }
void TestDeleteOk()        { g.venueLocId=kLocationId; Run("DELETE",kSubscriber, HTTP::HTTP_OK, nullptr,
    [](const FakeResponse&){
        ExpectEq(g.clearCalls,1,"clear called"); ExpectEq(g.deleteCalls,1,"delete called");
        ExpectEq(g.capturedLocId,kLocationId,"loc id"); ExpectEq(g.capturedVenueId,kVenueId,"venue id"); }); }
void TestDeleteClearFails(){ g.venueLocId=kLocationId; g.clearLocOk=false;
    Run("DELETE",kSubscriber, HTTP::HTTP_INTERNAL_SERVER_ERROR, nullptr,
    [](const FakeResponse&){ ExpectEq(g.deleteCalls,0,"no delete when clear fails"); }); }

const std::vector<std::pair<std::string,std::function<void()>>> kTests = {
    {"GetNoAuth",           TestGetNoAuth},
    {"GetVenuesFailure",    TestGetVenuesFailure},
    {"GetNoLocation",       TestGetNoLocation},
    {"GetOk",               TestGetOk},
    {"PostNoAuth",          TestPostNoAuth},
    {"PostNoTz",            TestPostNoTz},
    {"PostBadTz",           TestPostBadTz},
    {"PostDuplicate",       TestPostDuplicate},
    {"PostUnallowedField",  TestPostUnallowedField},
    {"PostOk",              TestPostOk},
    {"PutNoAuth",           TestPutNoAuth},
    {"PutEmptyBody",        TestPutEmptyBody},
    {"PutForbiddenFields",  TestPutForbiddenFields},
    {"PutBadTz",            TestPutBadTz},
    {"PutNoLocation",       TestPutNoLocation},
    {"PutOk",               TestPutOk},
    {"DeleteNoAuth",        TestDeleteNoAuth},
    {"DeleteNoLocation",    TestDeleteNoLocation},
    {"DeleteOk",            TestDeleteOk},
    {"DeleteClearFails",    TestDeleteClearFails},
};

} // namespace

int main() {
    int fail = 0;
    for (const auto &t : kTests) {
        try { Reset(); t.second(); std::cout << "[PASS] " << t.first << "\n"; }
        catch (const std::exception &e) { ++fail; std::cerr << "[FAIL] " << t.first << ": " << e.what() << "\n"; }
    }
    if (fail) { std::cerr << fail << " test(s) failed.\n"; return 1; }
    std::cout << kTests.size() << " test(s) passed.\n";
    return 0;
}
