//
//	License type: BSD 3-Clause License
//	License copy: https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/LICENSE
//
//	Created by Stephane Bourque on 2021-03-04.
//	Arilia Wireless Inc.
//


#include "Poco/URI.h"

#include "RESTAPI_server.h"
#include "Utils.h"
#include "RESTAPI_handler.h"

#include "RESTAPI_system_command.h"

namespace OpenWifi {

    class RESTAPI_server *RESTAPI_server::instance_ = nullptr;

    int RESTAPI_server::Start() {
        Logger_.information("Starting.");
        LogServer_.InitLogging();

        for(const auto & Svr: ConfigServersList_) {
            Logger_.information(Poco::format("Starting: %s:%s Keyfile:%s CertFile: %s", Svr.Address(), std::to_string(Svr.Port()),
                                             Svr.KeyFile(),Svr.CertFile()));

            auto Sock{Svr.CreateSecureSocket(Logger_)};

            Svr.LogCert(Logger_);
            if(!Svr.RootCA().empty())
                Svr.LogCas(Logger_);

            auto Params = new Poco::Net::HTTPServerParams;
            Params->setMaxThreads(50);
            Params->setMaxQueued(200);
            Params->setKeepAlive(true);

            auto NewServer = std::make_unique<Poco::Net::HTTPServer>(new RequestHandlerFactory(LogServer_), Pool_, Sock, Params);
            NewServer->start();
            RESTServers_.push_back(std::move(NewServer));
        }
        return 0;
    }

    Poco::Net::HTTPRequestHandler *RequestHandlerFactory::createRequestHandler(const Poco::Net::HTTPServerRequest & Request) {

        Poco::URI uri(Request.getURI());
        auto *Path = uri.getPath().c_str();
        RESTAPIHandler::BindingMap Bindings;

        return  RESTAPI_Router<
                RESTAPI_system_command
                >(Path,Bindings,Logger_, Server_);
    }

    void RESTAPI_server::Stop() {
        Logger_.information("Stopping ");
        for( const auto & svr : RESTServers_ )
            svr->stop();
        RESTServers_.clear();
    }

    void RESTAPI_server::reinitialize(Poco::Util::Application &self) {
        Logger_.information("Reinitializing.");
        Daemon()->LoadConfigurationFile();
        Stop();
        Start();
    }


}  // namespace