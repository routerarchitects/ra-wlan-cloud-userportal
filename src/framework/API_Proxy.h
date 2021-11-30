//
// Created by stephane bourque on 2021-11-30.
//

#pragma once

#include "framework/MicroService.h"

namespace OpenWifi {
    void API_Proxy(Poco::Net::HTTPServerRequest *Request,
                   Poco::Net::HTTPServerResponse *Response,
                   const char * ServiceType,
                   const char * PathRewrite,
                   uint64_t msTimeout_ = 10000 ) {
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

        auto Services = MicroService::instance().GetServices(ServiceType);
        std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
        for(auto const &Svc:Services) {
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
            Poco::URI   SourceURI(Request->getURI());
            Poco::URI	DestinationURI(Svc.PrivateEndPoint);
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

            DestinationURI.setPath(PathRewrite);
            DestinationURI.setQuery(SourceURI.getQuery());
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << DestinationURI.getHost() << ":" << DestinationURI.getPort() << std::endl;
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << DestinationURI.getPathAndQuery() << std::endl;

            Poco::Net::HTTPSClientSession Session(DestinationURI.getHost(), DestinationURI.getPort());
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

            Session.setTimeout(Poco::Timespan(msTimeout_/1000, msTimeout_ % 1000));
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

            Poco::Net::HTTPRequest ProxyRequest(Request->getMethod(),
                                                DestinationURI.getPathAndQuery(),
                                                Poco::Net::HTTPMessage::HTTP_1_1);
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
            ProxyRequest.add("X-API-KEY", Svc.AccessKey);
            ProxyRequest.add("X-INTERNAL-NAME", MicroService::instance().PublicEndPoint());
            ProxyRequest.setContentType("application/json");
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;
            std::ostream & os = Session.sendRequest(ProxyRequest);
            ProxyRequest.read(Request->stream());
            std::cout << __FILE__ << " : " << __func__  << " : " << __LINE__ << std::endl;

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
    }
}