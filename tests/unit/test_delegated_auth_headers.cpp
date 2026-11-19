/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Poco/JSON/Array.h"
#include "Poco/JSON/Object.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Logger.h"
#include "Poco/Net/HTTPRequestHandler.h"
#include "Poco/Net/HTTPRequestHandlerFactory.h"
#include "Poco/Net/HTTPServer.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/ServerSocket.h"
#include "Poco/Net/SocketAddress.h"

#include "framework/MicroServiceNames.h"
#include "framework/MicroServiceFuncs.h"
#include "framework/OpenAPIRequests.h"
#include "framework/API_Proxy.h"

namespace {

class TestFailure : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

inline void Expect(bool condition, const std::string &message) {
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename T, typename U>
inline void ExpectEq(const T &actual, const U &expected, const std::string &message) {
    if (!(actual == expected)) {
        std::ostringstream os;
        os << message << " expected=" << expected << " actual=" << actual;
        throw TestFailure(os.str());
    }
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

uint16_t g_serverPort = 0;
const std::string kFakeApiKey = "test-internal-api-key-999";
const std::string kFakeInternalName = "owsub-test-client-service";

struct CapturedRequest {
    std::string method;
    std::string uri;
    std::string apiKey;
    std::string internalName;
    std::string authorization;
    bool hasApiKey = false;
    bool hasInternalName = false;
    bool hasAuthorization = false;
};

std::vector<CapturedRequest> g_captured;
std::mutex g_mutex;

class TestRequestHandler : public Poco::Net::HTTPRequestHandler {
  public:
    void handleRequest(Poco::Net::HTTPServerRequest &request,
                       Poco::Net::HTTPServerResponse &response) override {
        CapturedRequest cr;
        cr.method = request.getMethod();
        cr.uri = request.getURI();
        cr.hasApiKey = request.has("X-API-KEY");
        if (cr.hasApiKey) {
            cr.apiKey = request.get("X-API-KEY");
        }
        cr.hasInternalName = request.has("X-INTERNAL-NAME");
        if (cr.hasInternalName) {
            cr.internalName = request.get("X-INTERNAL-NAME");
        }
        cr.hasAuthorization = request.has("Authorization");
        if (cr.hasAuthorization) {
            cr.authorization = request.get("Authorization");
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_captured.push_back(cr);
        }

        response.setStatus(Poco::Net::HTTPResponse::HTTP_OK);
        response.setContentType("application/json");
        response.setContentLength(2);
        std::ostream &os = response.send();
        os << "{}";
    }
};

class TestRequestHandlerFactory : public Poco::Net::HTTPRequestHandlerFactory {
  public:
    Poco::Net::HTTPRequestHandler *
    createRequestHandler(const Poco::Net::HTTPServerRequest &) override {
        return new TestRequestHandler();
    }
};

void ResetCaptured() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_captured.clear();
}

CapturedRequest GetLastCaptured() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_captured.empty()) {
        throw TestFailure("Expected at least 1 captured request, but none was recorded");
    }
    return g_captured.back();
}

} // namespace

namespace OpenWifi {
    Types::MicroServiceMetaVec MicroServiceGetServices(const std::string &Type) {
        Types::MicroServiceMetaVec res;
        Types::MicroServiceMeta meta;
        meta.PrivateEndPoint = "http://127.0.0.1:" + std::to_string(g_serverPort);
        meta.PublicEndPoint = "http://127.0.0.1:" + std::to_string(g_serverPort);
        meta.Type = Type;
        meta.AccessKey = kFakeApiKey;
        res.push_back(meta);
        return res;
    }

    std::string MicroServicePublicEndPoint() {
        return kFakeInternalName;
    }
}

#include "../../src/framework/OpenAPIRequests.cpp"

namespace {

void TestScenario1_InternalRequestWithoutToken() {
    ResetCaptured();
    OpenWifi::OpenAPIRequestGet req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/venue", {}, 10000);
    Poco::JSON::Object::Ptr res;
    auto status = req.Do(res, "");
    ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "GET without token should succeed");
    auto cr = GetLastCaptured();
    Expect(cr.hasApiKey, "X-API-KEY header must be present on internal request");
    ExpectEq(cr.apiKey, kFakeApiKey, "X-API-KEY value mismatch");
    Expect(cr.hasInternalName, "X-INTERNAL-NAME header must be present on internal request");
    ExpectEq(cr.internalName, kFakeInternalName, "X-INTERNAL-NAME value mismatch");
    Expect(!cr.hasAuthorization, "Authorization header must NOT be present on internal request without token");
    std::cout << "  [PASS] Scenario 1: Internal request without token sends internal credentials only" << std::endl;
}

void TestScenario2_SubscriberRequestWithToken() {
    ResetCaptured();
    OpenWifi::OpenAPIRequestGet req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/subscriberDevice/60cf84f22290", {}, 10000);
    Poco::JSON::Object::Ptr res;
    auto status = req.Do(res, "sub-token-xyz-12345");
    ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "GET with token should succeed");
    auto cr = GetLastCaptured();
    Expect(cr.hasApiKey, "X-API-KEY must be present on delegated subscriber request");
    ExpectEq(cr.apiKey, kFakeApiKey, "X-API-KEY value mismatch");
    Expect(cr.hasInternalName, "X-INTERNAL-NAME must be present on delegated subscriber request");
    ExpectEq(cr.internalName, kFakeInternalName, "X-INTERNAL-NAME value mismatch");
    Expect(cr.hasAuthorization, "Authorization header must be present on delegated subscriber request");
    ExpectEq(cr.authorization, "Bearer sub-token-xyz-12345", "Authorization header value mismatch");
    std::cout << "  [PASS] Scenario 2: Subscriber request with token sends internal credentials + Bearer" << std::endl;
}

void TestScenario3_ProxyRequestWithoutAuthorization() {
    ResetCaptured();
    FakeResponse fakeResp;
    FakeRequest fakeReq("DELETE", "/api/v1/subscriber/sub-123", "", fakeResp);
    OpenWifi::API_Proxy(Poco::Logger::get("test"), &fakeReq, &fakeResp,
                        OpenWifi::uSERVICE_PROVISIONING.c_str(), "/api/v1/subscriber/sub-123");
    auto cr = GetLastCaptured();
    Expect(cr.hasApiKey, "X-API-KEY must be present on proxy request without Auth");
    ExpectEq(cr.apiKey, kFakeApiKey, "X-API-KEY mismatch on proxy");
    Expect(cr.hasInternalName, "X-INTERNAL-NAME must be present on proxy request without Auth");
    ExpectEq(cr.internalName, kFakeInternalName, "X-INTERNAL-NAME mismatch on proxy");
    Expect(!cr.hasAuthorization, "Authorization header must NOT be present when incoming request lacks Auth");
    std::cout << "  [PASS] Scenario 3: Proxy request without Authorization sends internal credentials only" << std::endl;
}

void TestScenario4_ProxyRequestWithAuthorization() {
    ResetCaptured();
    FakeResponse fakeResp;
    FakeRequest fakeReq("DELETE", "/api/v1/subscriber/sub-123", "", fakeResp);
    fakeReq.set("Authorization", "Bearer forwarded-proxy-token-777");
    OpenWifi::API_Proxy(Poco::Logger::get("test"), &fakeReq, &fakeResp,
                        OpenWifi::uSERVICE_PROVISIONING.c_str(), "/api/v1/subscriber/sub-123");
    auto cr = GetLastCaptured();
    Expect(cr.hasApiKey, "X-API-KEY must be present on proxy request with Auth");
    ExpectEq(cr.apiKey, kFakeApiKey, "X-API-KEY mismatch on proxy");
    Expect(cr.hasInternalName, "X-INTERNAL-NAME must be present on proxy request with Auth");
    ExpectEq(cr.internalName, kFakeInternalName, "X-INTERNAL-NAME mismatch on proxy");
    Expect(cr.hasAuthorization, "Authorization header must be present on proxy request with Auth");
    ExpectEq(cr.authorization, "Bearer forwarded-proxy-token-777", "Forwarded Authorization value mismatch");
    std::cout << "  [PASS] Scenario 4: Proxy request with Authorization sends internal credentials + original Authorization" << std::endl;
}

void TestScenario5_EmptyBearerTokenOmitsHeader() {
    ResetCaptured();
    Poco::JSON::Object body;
    OpenWifi::OpenAPIRequestPost req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/subscriberDevice/0", {}, body, 10000);
    Poco::JSON::Object::Ptr res;
    auto status = req.Do(res, "");
    ExpectEq(status, Poco::Net::HTTPResponse::HTTP_OK, "POST with empty token should succeed");
    auto cr = GetLastCaptured();
    Expect(cr.hasApiKey, "X-API-KEY must be present");
    Expect(cr.hasInternalName, "X-INTERNAL-NAME must be present");
    Expect(!cr.hasAuthorization, "Authorization header must NOT be present for empty bearer token string");
    std::cout << "  [PASS] Scenario 5: Empty bearer token string produces no Authorization header" << std::endl;
}

void TestScenario6_AllHttpMethodsDelegatedAuthPolicy() {
    // 1. GET (Object overload)
    {
        ResetCaptured();
        OpenWifi::OpenAPIRequestGet req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/obj-test", {}, 10000);
        Poco::JSON::Object::Ptr res;
        req.Do(res, "method-token-1");
        auto cr = GetLastCaptured();
        ExpectEq(cr.method, "GET", "Expected GET method");
        Expect(cr.hasApiKey && cr.hasInternalName && cr.hasAuthorization, "GET (object) with token must have internal + Bearer");
        ExpectEq(cr.authorization, "Bearer method-token-1", "GET (object) Bearer token mismatch");

        ResetCaptured();
        req.Do(res, "");
        cr = GetLastCaptured();
        Expect(cr.hasApiKey && cr.hasInternalName && !cr.hasAuthorization, "GET (object) without token must have internal only");
    }

    // 2. GET (Array overload)
    {
        ResetCaptured();
        OpenWifi::OpenAPIRequestGet req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/arr-test", {}, 10000);
        Poco::JSON::Array::Ptr arr;
        Poco::JSON::Object::Ptr obj;
        req.Do(arr, obj, "method-token-2");
        auto cr = GetLastCaptured();
        ExpectEq(cr.method, "GET", "Expected GET method");
        Expect(cr.hasApiKey && cr.hasInternalName && cr.hasAuthorization, "GET (array) with token must have internal + Bearer");
        ExpectEq(cr.authorization, "Bearer method-token-2", "GET (array) Bearer token mismatch");

        ResetCaptured();
        req.Do(arr, obj, "");
        cr = GetLastCaptured();
        Expect(cr.hasApiKey && cr.hasInternalName && !cr.hasAuthorization, "GET (array) without token must have internal only");
    }

    // 3. POST
    {
        ResetCaptured();
        Poco::JSON::Object body;
        OpenWifi::OpenAPIRequestPost req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/post-test", {}, body, 10000);
        Poco::JSON::Object::Ptr res;
        req.Do(res, "method-token-3");
        auto cr = GetLastCaptured();
        ExpectEq(cr.method, "POST", "Expected POST method");
        Expect(cr.hasApiKey && cr.hasInternalName && cr.hasAuthorization, "POST with token must have internal + Bearer");
        ExpectEq(cr.authorization, "Bearer method-token-3", "POST Bearer token mismatch");

        ResetCaptured();
        req.Do(res, "");
        cr = GetLastCaptured();
        Expect(cr.hasApiKey && cr.hasInternalName && !cr.hasAuthorization, "POST without token must have internal only");
    }

    // 4. PUT
    {
        ResetCaptured();
        Poco::JSON::Object body;
        OpenWifi::OpenAPIRequestPut req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/put-test", {}, body, 10000);
        Poco::JSON::Object::Ptr res;
        req.Do(res, "method-token-4");
        auto cr = GetLastCaptured();
        ExpectEq(cr.method, "PUT", "Expected PUT method");
        Expect(cr.hasApiKey && cr.hasInternalName && cr.hasAuthorization, "PUT with token must have internal + Bearer");
        ExpectEq(cr.authorization, "Bearer method-token-4", "PUT Bearer token mismatch");

        ResetCaptured();
        req.Do(res, "");
        cr = GetLastCaptured();
        Expect(cr.hasApiKey && cr.hasInternalName && !cr.hasAuthorization, "PUT without token must have internal only");
    }

    // 5. DELETE
    {
        ResetCaptured();
        OpenWifi::OpenAPIRequestDelete req(OpenWifi::uSERVICE_PROVISIONING, "/api/v1/delete-test", {}, 10000);
        Poco::JSON::Object::Ptr res;
        req.Do(res, "method-token-5");
        auto cr = GetLastCaptured();
        ExpectEq(cr.method, "DELETE", "Expected DELETE method");
        Expect(cr.hasApiKey && cr.hasInternalName && cr.hasAuthorization, "DELETE with token must have internal + Bearer");
        ExpectEq(cr.authorization, "Bearer method-token-5", "DELETE Bearer token mismatch");

        ResetCaptured();
        req.Do(res, "");
        cr = GetLastCaptured();
        Expect(cr.hasApiKey && cr.hasInternalName && !cr.hasAuthorization, "DELETE without token must have internal only");
    }

    std::cout << "  [PASS] Scenario 6: All HTTP methods (GET obj, GET arr, POST, PUT, DELETE) adhere to identical policy" << std::endl;
}

} // namespace

int main() {
    try {
        std::cout << "=== Running Delegated Authentication Headers Regression Tests ===" << std::endl;

        Poco::Net::ServerSocket socket(0);
        socket.listen(16);
        g_serverPort = socket.address().port();

        auto params = new Poco::Net::HTTPServerParams();
        params->setMaxThreads(4);
        Poco::Net::HTTPServer server(new TestRequestHandlerFactory(), socket, params);
        server.start();

        TestScenario1_InternalRequestWithoutToken();
        TestScenario2_SubscriberRequestWithToken();
        TestScenario3_ProxyRequestWithoutAuthorization();
        TestScenario4_ProxyRequestWithAuthorization();
        TestScenario5_EmptyBearerTokenOmitsHeader();
        TestScenario6_AllHttpMethodsDelegatedAuthPolicy();

        server.stop();
        std::cout << "=== ALL 6 DELEGATED AUTHENTICATION SCENARIOS PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << "TEST FAILURE: " << ex.what() << std::endl;
        return 1;
    }
}
