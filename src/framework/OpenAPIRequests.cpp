/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */
//
// Created by stephane bourque on 2022-10-25.
//

#include "OpenAPIRequests.h"

#include "Poco/JSON/Parser.h"
#include "Poco/Logger.h"
#include "Poco/Net/HTTPRequest.h"
#include "Poco/Net/HTTPSClientSession.h"
#include "Poco/URI.h"

#include <sstream>

#include "fmt/format.h"
#include "framework/MicroServiceFuncs.h"

namespace OpenWifi {
	namespace {
		// Reads the raw HTTP response stream into a string.
		// Placed in an anonymous namespace to give it internal linkage,
		// ensuring it's only visible within this file and preventing naming 
		// collisions with other files during compilation.
		// This is specifically needed to extract the raw `config-raw` snapshot 
		// from DELETE responses before attempting to parse the body.
		std::string ReadResponseBody(std::istream &is) {
			std::ostringstream os;
			os << is.rdbuf();
			return os.str();
		}
	} // namespace

	Poco::Net::HTTPServerResponse::HTTPStatus
	OpenAPIRequestGet::Do(Poco::JSON::Object::Ptr &ResponseObject, const std::string &BearerToken) {
		try {

			auto Services = MicroServiceGetServices(Type_);
			for (auto const &Svc : Services) {
				Poco::URI URI(Svc.PrivateEndPoint);

				auto Secure = (URI.getScheme() == "https");

				URI.setPath(EndPoint_);
				for (const auto &qp : QueryData_)
					URI.addQueryParameter(qp.first, qp.second);

				std::string Path(URI.getPathAndQuery());
				Poco::Net::HTTPRequest Request(Poco::Net::HTTPRequest::HTTP_GET, Path,
											   Poco::Net::HTTPMessage::HTTP_1_1);

				poco_debug(Poco::Logger::get("REST-CALLER-GET"),
						   fmt::format(" {}", LoggingStr_.empty() ? URI.toString() : LoggingStr_));

				if (BearerToken.empty()) {
					Request.add("X-API-KEY", Svc.AccessKey);
					Request.add("X-INTERNAL-NAME", MicroServicePublicEndPoint());
				} else {
					// Authorization: Bearer ${token}
					Request.add("Authorization", "Bearer " + BearerToken);
				}

				if (Secure) {
					Poco::Net::HTTPSClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					Session.sendRequest(Request);

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					try {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} catch (...) {
					}
					return Response.getStatus();
				} else {
					Poco::Net::HTTPClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					Session.sendRequest(Request);

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					try {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} catch (...) {
					}
					return Response.getStatus();
				}
			}
		} catch (const Poco::Exception &E) {
			Poco::Logger::get("REST-CALLER-GET").log(E);
		}
		return Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT;
	}

	Poco::Net::HTTPServerResponse::HTTPStatus
	OpenAPIRequestPut::Do(Poco::JSON::Object::Ptr &ResponseObject, const std::string &BearerToken) {
		try {
			auto Services = MicroServiceGetServices(Type_);
			for (auto const &Svc : Services) {
				Poco::URI URI(Svc.PrivateEndPoint);

				auto Secure = (URI.getScheme() == "https");

				URI.setPath(EndPoint_);
				for (const auto &qp : QueryData_)
					URI.addQueryParameter(qp.first, qp.second);

				poco_debug(Poco::Logger::get("REST-CALLER-PUT"),
						   fmt::format(" {}", LoggingStr_.empty() ? URI.toString() : LoggingStr_));

				std::string Path(URI.getPathAndQuery());

				Poco::Net::HTTPRequest Request(Poco::Net::HTTPRequest::HTTP_PUT, Path,
											   Poco::Net::HTTPMessage::HTTP_1_1);
				std::ostringstream obody;
				Poco::JSON::Stringifier::stringify(Body_, obody);

				Request.setContentType("application/json");
				Request.setContentLength(obody.str().size());

				if (BearerToken.empty()) {
					Request.add("X-API-KEY", Svc.AccessKey);
					Request.add("X-INTERNAL-NAME", MicroServicePublicEndPoint());
				} else {
					// Authorization: Bearer ${token}
					Request.add("Authorization", "Bearer " + BearerToken);
				}

				if (Secure) {
					Poco::Net::HTTPSClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					std::ostream &os = Session.sendRequest(Request);
					os << obody.str();

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					if (Response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} else {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					}
					return Response.getStatus();
				} else {
					Poco::Net::HTTPClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					std::ostream &os = Session.sendRequest(Request);
					os << obody.str();

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					if (Response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} else {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					}
					return Response.getStatus();
				}
			}
		} catch (const Poco::Exception &E) {
			Poco::Logger::get("REST-CALLER-PUT").log(E);
		}
		return Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT;
	}

	Poco::Net::HTTPServerResponse::HTTPStatus
	OpenAPIRequestPost::Do(Poco::JSON::Object::Ptr &ResponseObject,
						   const std::string &BearerToken) {
		try {
			auto Services = MicroServiceGetServices(Type_);

			for (auto const &Svc : Services) {
				Poco::URI URI(Svc.PrivateEndPoint);

				auto Secure = (URI.getScheme() == "https");

				URI.setPath(EndPoint_);
				for (const auto &qp : QueryData_)
					URI.addQueryParameter(qp.first, qp.second);

				poco_debug(Poco::Logger::get("REST-CALLER-POST"),
						   fmt::format(" {}", LoggingStr_.empty() ? URI.toString() : LoggingStr_));

				std::string Path(URI.getPathAndQuery());

				Poco::Net::HTTPRequest Request(Poco::Net::HTTPRequest::HTTP_POST, Path,
											   Poco::Net::HTTPMessage::HTTP_1_1);
				std::ostringstream obody;
				Poco::JSON::Stringifier::stringify(Body_, obody);

				Request.setContentType("application/json");
				Request.setContentLength(obody.str().size());

				if (BearerToken.empty()) {
					Request.add("X-API-KEY", Svc.AccessKey);
					Request.add("X-INTERNAL-NAME", MicroServicePublicEndPoint());
				} else {
					// Authorization: Bearer ${token}
					Request.add("Authorization", "Bearer " + BearerToken);
				}

				if (Secure) {
					Poco::Net::HTTPSClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));
					std::ostream &os = Session.sendRequest(Request);
					os << obody.str();

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					if (Response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} else {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					}
					return Response.getStatus();
				} else {
					Poco::Net::HTTPClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));
					std::ostream &os = Session.sendRequest(Request);
					os << obody.str();

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					if (Response.getStatus() == Poco::Net::HTTPResponse::HTTP_OK) {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					} else {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(is).extract<Poco::JSON::Object::Ptr>();
					}
					return Response.getStatus();
				}
			}
		} catch (const Poco::Exception &E) {
			Poco::Logger::get("REST-CALLER-POST").log(E);
		}
		return Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT;
	}

	Poco::Net::HTTPServerResponse::HTTPStatus
	OpenAPIRequestDelete::Do(Poco::JSON::Object::Ptr &ResponseObject, const std::string &BearerToken) {
		std::string rawResponseBody;
		return Do(ResponseObject, rawResponseBody, BearerToken);
	}

	Poco::Net::HTTPServerResponse::HTTPStatus OpenAPIRequestDelete::Do(
		Poco::JSON::Object::Ptr &ResponseObject, std::string &RawResponseBody,
		const std::string &BearerToken) {
		try {
			auto Services = MicroServiceGetServices(Type_);

			for (auto const &Svc : Services) {
				Poco::URI URI(Svc.PrivateEndPoint);

				auto Secure = (URI.getScheme() == "https");

				URI.setPath(EndPoint_);
				for (const auto &qp : QueryData_)
					URI.addQueryParameter(qp.first, qp.second);

				poco_debug(Poco::Logger::get("REST-CALLER-DELETE"),
						   fmt::format(" {}", LoggingStr_.empty() ? URI.toString() : LoggingStr_));

				std::string Path(URI.getPathAndQuery());

				Poco::Net::HTTPRequest Request(Poco::Net::HTTPRequest::HTTP_DELETE, Path,
											   Poco::Net::HTTPMessage::HTTP_1_1);
				if (BearerToken.empty()) {
					Request.add("X-API-KEY", Svc.AccessKey);
					Request.add("X-INTERNAL-NAME", MicroServicePublicEndPoint());
				} else {
					// Authorization: Bearer ${token}
					Request.add("Authorization", "Bearer " + BearerToken);
				}

				if (Secure) {
					Poco::Net::HTTPSClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));
					Session.sendRequest(Request);
					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					RawResponseBody = ReadResponseBody(is);
					try {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(RawResponseBody).extract<Poco::JSON::Object::Ptr>();
					} catch (...) {
					}
					return Response.getStatus();
				} else {
					Poco::Net::HTTPClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));
					Session.sendRequest(Request);
					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					RawResponseBody = ReadResponseBody(is);
					try {
						Poco::JSON::Parser P;
						ResponseObject = P.parse(RawResponseBody).extract<Poco::JSON::Object::Ptr>();
					} catch (...) {
					}
					return Response.getStatus();
				}
			}
		} catch (const Poco::Exception &E) {
			Poco::Logger::get("REST-CALLER-DELETE").log(E);
		}
		return Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT;
	}

	Poco::Net::HTTPServerResponse::HTTPStatus
	OpenAPIRequestGet::Do(Poco::JSON::Array::Ptr &ResponseArray,
						  Poco::JSON::Object::Ptr &ResponseObject,
						  const std::string &BearerToken) {
		try {

			auto Services = MicroServiceGetServices(Type_);
			for (auto const &Svc : Services) {
				Poco::URI URI(Svc.PrivateEndPoint);

				auto Secure = (URI.getScheme() == "https");

				URI.setPath(EndPoint_);
				for (const auto &qp : QueryData_)
					URI.addQueryParameter(qp.first, qp.second);

				std::string Path(URI.getPathAndQuery());
				Poco::Net::HTTPRequest Request(Poco::Net::HTTPRequest::HTTP_GET, Path,
											   Poco::Net::HTTPMessage::HTTP_1_1);

				poco_debug(Poco::Logger::get("REST-CALLER-GET"),
						   fmt::format(" {}", LoggingStr_.empty() ? URI.toString() : LoggingStr_));

				if (BearerToken.empty()) {
					Request.add("X-API-KEY", Svc.AccessKey);
					Request.add("X-INTERNAL-NAME", MicroServicePublicEndPoint());
				} else {
					Request.add("Authorization", "Bearer " + BearerToken);
				}

				if (Secure) {
					Poco::Net::HTTPSClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					Session.sendRequest(Request);

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					try {
						Poco::JSON::Parser P;
						auto parsed = P.parse(is);
						if (parsed.type() == typeid(Poco::JSON::Array::Ptr)) {
							ResponseArray = parsed.extract<Poco::JSON::Array::Ptr>();
						} else {
							ResponseObject = parsed.extract<Poco::JSON::Object::Ptr>();
						}
					} catch (...) {
					}
					return Response.getStatus();
				} else {
					Poco::Net::HTTPClientSession Session(URI.getHost(), URI.getPort());
					Session.setTimeout(Poco::Timespan(msTimeout_ / 1000, msTimeout_ % 1000));

					Session.sendRequest(Request);

					Poco::Net::HTTPResponse Response;
					std::istream &is = Session.receiveResponse(Response);
					try {
						Poco::JSON::Parser P;
						auto parsed = P.parse(is);
						if (parsed.type() == typeid(Poco::JSON::Array::Ptr)) {
							ResponseArray = parsed.extract<Poco::JSON::Array::Ptr>();
						} else {
							ResponseObject = parsed.extract<Poco::JSON::Object::Ptr>();
						}
					} catch (...) {
					}
					return Response.getStatus();
				}
			}
		} catch (const Poco::Exception &E) {
			Poco::Logger::get("REST-CALLER-GET").log(E);
		}
		return Poco::Net::HTTPServerResponse::HTTP_GATEWAY_TIMEOUT;
	}

} // namespace OpenWifi
