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

        auto Services = MicroService::instance().GetServices(ServiceType);
        for(auto const &Svc:Services) {
            Poco::URI   SourceURI(Request->getURI());
            Poco::URI	DestinationURI(Svc.PrivateEndPoint);

            DestinationURI.setPath(PathRewrite);
            DestinationURI.setQuery(SourceURI.getQuery());

            Poco::Net::HTTPSClientSession Session(DestinationURI.getHost(), DestinationURI.getPort());

            Session.setTimeout(Poco::Timespan(msTimeout_/1000, msTimeout_ % 1000));

            Poco::Net::HTTPRequest ProxyRequest(Request->getMethod(),
                                                DestinationURI.getPathAndQuery(),
                                                Poco::Net::HTTPMessage::HTTP_1_1);
            ProxyRequest.add("X-API-KEY", Svc.AccessKey);
            ProxyRequest.add("X-INTERNAL-NAME", MicroService::instance().PublicEndPoint());
            ProxyRequest.setContentType("application/json");
            std::ostream & os = Session.sendRequest(ProxyRequest);
            ProxyRequest.read(Request->stream());

            Poco::Net::HTTPResponse ProxyResponse;
            std::istream &is = Session.receiveResponse(ProxyResponse);

            if(ProxyResponse.getStatus()==Poco::Net::HTTPResponse::HTTP_OK) {
                std::stringstream   SS;
                ProxyResponse.write(SS);
                Response->read(SS);
                Response->setStatus(Poco::Net::HTTPResponse::HTTP_OK);
                return;
            }
        }
    }
}