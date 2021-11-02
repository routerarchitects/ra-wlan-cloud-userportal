//
// Created by stephane bourque on 2021-11-01.
//

#ifndef OWSUB_STORAGE_RESERVATIONS_H
#define OWSUB_STORAGE_RESERVATIONS_H

#include "framework/orm.h"
#include "RESTObjects/RESTAPI_SubObjects.h"

namespace OpenWifi {

    typedef Poco::Tuple<
        std::string,        //  uuid
        std::string,        //  reservations
        uint64_t,           //  created
        uint64_t            //  modified
    > ReservationDBRecordType;

    class ReservationsDB : public ORM::DB<ReservationDBRecordType,SubObjects::IPReservationList> {
    public:
        ReservationsDB( OpenWifi::DBType T, Poco::Data::SessionPool & P, Poco::Logger &L);
    private:
    };

}

#endif //OWSUB_STORAGE_RESERVATIONS_H
