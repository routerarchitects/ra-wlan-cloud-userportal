/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#pragma once

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "Poco/JSON/Object.h"
#include "Poco/JSON/Array.h"
#include "Poco/JSON/Parser.h"
#include "Poco/Logger.h"
#include "Poco/Net/HTTPServerParams.h"
#include "Poco/Net/HTTPServerRequest.h"
#include "Poco/Net/HTTPServerResponse.h"
#include "Poco/Net/SocketAddress.h"

#include "framework/MicroService.h"
#include "framework/RESTAPI_GenericServerAccounting.h"
#include "framework/AuthClient.h"
#include "RESTAPI/RESTAPI_parental_control_utils.h"
#include "sdks/SDK_parental_control.h"

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

inline Poco::JSON::Object::Ptr ParseObject(const std::string &body) {
    Poco::JSON::Parser parser;
    return parser.parse(body).extract<Poco::JSON::Object::Ptr>();
}

inline Poco::JSON::Array::Ptr ParseArray(const std::string &body) {
    Poco::JSON::Parser parser;
    return parser.parse(body).extract<Poco::JSON::Array::Ptr>();
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

template <typename HandlerType>
inline void RunHandlerRequest(
    const std::string &method,
    const std::string &uri,
    const std::string &body,
    const std::map<std::string, std::string> &bindings,
    const std::string &subscriberId,
    const std::string &operatorId,
    Poco::Net::HTTPResponse::HTTPStatus expectedStatus,
    const std::function<void(HandlerType &)> &customSetup = nullptr,
    const std::function<void(const FakeResponse &)> &assertions = nullptr
) {
    static OpenWifi::RESTAPI_GenericServerAccounting server;
    auto &logger = Poco::Logger::get("test_handlers");
    HandlerType handler(bindings, logger, server, 1, false);
    if (!subscriberId.empty()) {
        handler.UserInfo_.userinfo.id = subscriberId;
    }
    if (!operatorId.empty()) {
        handler.UserInfo_.userinfo.owner = operatorId;
    }
    if (customSetup) {
        customSetup(handler);
    }
    FakeResponse response;
    FakeRequest request(method, uri, body, response);
    handler.Request = &request;
    handler.Response = &response;

    if (method == Poco::Net::HTTPRequest::HTTP_GET) {
        handler.DoGet();
    } else if (method == Poco::Net::HTTPRequest::HTTP_POST) {
        handler.DoPost();
    } else if (method == Poco::Net::HTTPRequest::HTTP_PUT) {
        handler.DoPut();
    } else if (method == Poco::Net::HTTPRequest::HTTP_DELETE) {
        handler.DoDelete();
    }

    ExpectEq(static_cast<int>(response.getStatus()), static_cast<int>(expectedStatus),
             "HTTP status mismatch for " + method + " " + uri);
    if (assertions) {
        assertions(response);
    }
}

namespace Poco::Util { class Application; }
namespace Poco::Net { class HTTPRequestHandler; }
namespace OpenWifi {
    class RESTAPI_GenericServerAccounting;
    inline void DaemonPostInitialization(Poco::Util::Application &) {}
    inline Poco::Net::HTTPRequestHandler* RESTAPI_ExtRouter(const std::string &, std::map<std::string, std::string> &, Poco::Logger &, RESTAPI_GenericServerAccounting &, unsigned long) {
        return nullptr;
    }
    inline Poco::Net::HTTPRequestHandler* RESTAPI_IntRouter(const std::string &, std::map<std::string, std::string> &, Poco::Logger &, RESTAPI_GenericServerAccounting &, unsigned long) {
        return nullptr;
    }

    inline SubSystemServer::SubSystemServer(const std::string &Name, const std::string &LoggingPrefix,
                                     const std::string &SubSystemConfigPrefix)
        : Name_(Name), LoggerPrefix_(LoggingPrefix),
          Logger_(std::make_unique<LoggerWrapper>(Poco::Logger::get(LoggingPrefix))),
          SubSystemConfigPrefix_(SubSystemConfigPrefix) {}

    inline void SubSystemServer::initialize(Poco::Util::Application &) {}

    inline bool AllowExternalMicroServices() { return false; }
    inline bool MicroServiceIsValidAPIKEY(const Poco::Net::HTTPServerRequest &) { return false; }
    inline bool AuthClient::IsValidApiKey(const std::string &, SecurityObjects::UserInfoAndPolicy &, unsigned long, bool &, bool &, bool &) { return false; }
    inline bool AuthClient::IsAuthorized(const std::string &, SecurityObjects::UserInfoAndPolicy &, unsigned long, bool &, bool &, bool) { return false; }
}

namespace OpenWifi::Utils {
    inline std::string FormatIPv6(const std::string &addr) { return addr; }
    inline bool ValidUUID(const std::string &uuid) {
        return uuid.size() == 36 && uuid[8] == '-' && uuid[13] == '-' && uuid[18] == '-' && uuid[23] == '-';
    }
}
