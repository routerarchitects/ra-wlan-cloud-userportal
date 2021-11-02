//
// Created by stephane bourque on 2021-11-01.
//

#include "storage_reservations.h"

namespace OpenWifi {

    static  ORM::FieldVec    ReservationDB_Fields{
        // object info
        ORM::Field{"id",64, true},
        ORM::Field{"reservations",ORM::FieldType::FT_TEXT},
        ORM::Field{"created",ORM::FieldType::FT_BIGINT},
        ORM::Field{"modified",ORM::FieldType::FT_BIGINT}
    };

    static  ORM::IndexVec    ContactDB_Indexes{
    };

    ReservationsDB::ReservationsDB( OpenWifi::DBType T, Poco::Data::SessionPool & P, Poco::Logger &L) :
    DB(T, "reservations", ReservationDB_Fields, ContactDB_Indexes, P, L, "res") {}

}

template<> void ORM::DB<OpenWifi::ReservationDBRecordType, OpenWifi::SubObjects::IPReservationList>::Convert(OpenWifi::ReservationDBRecordType &In, OpenWifi::SubObjects::IPReservationList &Out) {
    Out.id = In.get<0>();
    Out.reservations = OpenWifi::RESTAPI_utils::to_object_array<OpenWifi::SubObjects::IPReservation>(In.get<1>());
    Out.created = In.get<2>();
    Out.modified = In.get<3>();
}

template<> void ORM::DB<OpenWifi::ReservationDBRecordType, OpenWifi::SubObjects::IPReservationList>::Convert(OpenWifi::SubObjects::IPReservationList &In, OpenWifi::ReservationDBRecordType &Out) {
    Out.set<0>(In.id);
    Out.set<1>(OpenWifi::RESTAPI_utils::to_string(In.reservations));
    Out.set<2>(In.created);
    Out.set<3>(In.modified);
}
