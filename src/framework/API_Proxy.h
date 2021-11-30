//
// Created by stephane bourque on 2021-11-30.
//

#pragma once

#include "framework/MicroService.h"
#include "Poco/JSON/Parser.h"

namespace OpenWifi {
    void API_Proxy( Poco::Logger &Logger,
                    Poco::Net::HTTPServerRequest *Request,
                    Poco::Net::HTTPServerResponse *Response,
                    const char * ServiceType,
                    const char * PathRewrite,
                    uint64_t msTimeout_ = 10000 ) {
        try {
            auto Services = MicroService::instance().GetServices(ServiceType);
            for(auto const &Svc:Services) {
                Poco::URI   SourceURI(Request->getURI());
                Poco::URI	DestinationURI(Svc.PrivateEndPoint);
                DestinationURI.setPath(PathRewrite);
                DestinationURI.setQuery(SourceURI.getQuery());
                std::cout << DestinationURI.getHost() << ":" << DestinationURI.getPort() << "/" << DestinationURI.getPathAndQuery() << std::endl;

                Poco::Net::HTTPSClientSession Session(DestinationURI.getHost(), DestinationURI.getPort());

                Session.setTimeout(Poco::Timespan(msTimeout_/1000, msTimeout_ % 1000));
                Poco::Net::HTTPRequest ProxyRequest(Request->getMethod(),
                                                    DestinationURI.getPathAndQuery(),
                                                    Poco::Net::HTTPMessage::HTTP_1_1);
                ProxyRequest.add("X-API-KEY", Svc.AccessKey);
                ProxyRequest.add("X-INTERNAL-NAME", MicroService::instance().PublicEndPoint());
                ProxyRequest.setContentType("application/json");

                Poco::JSON::Parser P;
                auto Body = P.parse(Request->stream()).extract<Poco::JSON::Object::Ptr>();
                std::stringstream SS;
                Poco::JSON::Stringifier::stringify(Body,SS);

                std::ostream & os = Session.sendRequest(ProxyRequest);
                os << SS.str() ;

                for(const auto &i:ProxyRequest) {
                    std::cout << "   " << i.first << ":" << i.second << std::endl;
                }

                Poco::Net::HTTPResponse ProxyResponse;
                std::istream &is = Session.receiveResponse(ProxyResponse);
                std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

                if(ProxyResponse.getStatus()==Poco::Net::HTTPResponse::HTTP_OK) {
                    std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
                    std::stringstream   SS;
                    ProxyResponse.write(SS);
                    std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
                    Response->read(SS);
                    Response->setStatus(Poco::Net::HTTPResponse::HTTP_OK);
                    std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
                    return;
                }
            }
        } catch (const Poco::Exception &E) {
            Logger.log(E);
            std::cout << "EXC: " << E.what() << " " << E.message() << std::endl;
        }
    }
}