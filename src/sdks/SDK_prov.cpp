/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

//
// Created by stephane bourque on 2022-01-11.
//

#include "SDK_prov.h"
#include "framework/MicroServiceNames.h"
#include "framework/OpenAPIRequests.h"
#include "framework/RESTAPI_utils.h"

namespace OpenWifi::SDK::Prov {

	namespace Device {
		bool Get(RESTAPIHandler *client, const std::string &Mac,
				 ProvObjects::InventoryTag &Device) {
			std::string EndPoint = "/api/v1/inventory/" + Mac;

			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			auto CallResponse = Poco::makeShared<Poco::JSON::Object>();

			auto ResponseStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (ResponseStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Device.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}
	} // namespace Device

	namespace Venue {
		bool Get(RESTAPIHandler *client, const std::string &VenueUUID, ProvObjects::Venue &Venue,
				 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
				 Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue/" + VenueUUID;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Venue.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool GetVenues(RESTAPIHandler *client, const std::string &SubscriberId,
					   ProvObjects::VenueList &venueList,
					   Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
					   Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue";
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint,
										 {{"subscriberId", SubscriberId}}, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return venueList.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool CreateLocation(RESTAPIHandler *client, const std::string &VenueId,
							const Poco::JSON::Object::Ptr &locationData,
							Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
							Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue/" + VenueId;
			Poco::JSON::Object body;
			Poco::JSON::Object createObjects;
			Poco::JSON::Array objects;
			Poco::JSON::Object locationWrapper;

			if (locationData) {
				locationWrapper.set("location", *locationData);
			} else {
				Poco::JSON::Object emptyLocation;
				locationWrapper.set("location", emptyLocation);
			}
			objects.add(locationWrapper);
			createObjects.set("objects", objects);
			body.set("createObjects", createObjects);

			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, body, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}

		bool ClearLocation(RESTAPIHandler *client, const std::string &VenueId,
						   Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
						   Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue/" + VenueId;
			Poco::JSON::Object body;
			body.set("location", "");

			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, body, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}

		bool SetLocation(RESTAPIHandler *client, const std::string &VenueId,
						 const std::string &LocationUUID,
						 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
						 Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/venue/" + VenueId;
			Poco::JSON::Object body;
			body.set("location", LocationUUID);

			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, body, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}
	} // namespace Venue

	namespace Location {
		bool Get(RESTAPIHandler *client, const std::string &LocationUUID, ProvObjects::Location &Location,
				 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
				 Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/location/" + LocationUUID;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return Location.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool Put(RESTAPIHandler *client, const std::string &LocationUUID,
				 const Poco::JSON::Object::Ptr &body,
				 Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
				 Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/location/" + LocationUUID;
			Poco::JSON::Object reqBody;
			if (body) {
				reqBody = *body;
			}
			auto API = OpenAPIRequestPut(uSERVICE_PROVISIONING, EndPoint, {}, reqBody, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}

		bool Delete(RESTAPIHandler *client, const std::string &LocationUUID,
					Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
					Poco::JSON::Object::Ptr &CallResponse) {
			const std::string EndPoint = "/api/v1/location/" + LocationUUID;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallResponse = Poco::makeShared<Poco::JSON::Object>();
			CallStatus = API.Do(CallResponse,
								client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			return CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK;
		}
	} // namespace Location

	namespace Subscriber {
		bool GetDevices(RESTAPIHandler *client, const std::string &SubscriberId, const std::string &OperatorId,
						ProvObjects::SubscriberDeviceList &devList,
						Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
						Poco::JSON::Object::Ptr &CallResponse) {

			std::string EndPoint = "/api/v1/subscriberDevice";
			auto API = OpenAPIRequestGet(
				uSERVICE_PROVISIONING, EndPoint,
				{{"subscriberId", SubscriberId}, {"operatorId", OperatorId}}, 60000);
			CallStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return devList.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool GetDevice(RESTAPIHandler *client, const std::string &SerialNumber,
					   ProvObjects::SubscriberDevice &device,
					   Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
					   Poco::JSON::Object::Ptr &CallResponse) {
			std::string EndPoint = "/api/v1/subscriberDevice/" + SerialNumber;
			auto API = OpenAPIRequestGet(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallStatus = API.Do(
				CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus == Poco::Net::HTTPServerResponse::HTTP_OK) {
				try {
					return device.from_json(CallResponse);
				} catch (...) {
					return false;
				}
			}
			return false;
		}

		bool CreateSubsciberDevice(
			RESTAPIHandler *client, const std::string &name, const std::string &serialNumber,
			const std::string &subscriberId, const std::string &operatorId, const std::string &deviceGroup,
			const ProvObjects::DeviceConfigurationElementVec &configuration,
			ProvObjects::SubscriberDevice &device,
			Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus,
			Poco::JSON::Object::Ptr &CallResponse) {
			std::string EndPoint = "/api/v1/subscriberDevice/0";
			Poco::JSON::Object Body;
			Body.set("name", name);
			Body.set("serialNumber", serialNumber);
			Body.set("subscriberId", subscriberId);
			Body.set("operatorId", operatorId);
			Body.set("deviceGroup", deviceGroup);
			RESTAPI_utils::field_to_json(Body, "configuration", configuration);
			auto API = OpenAPIRequestPost(uSERVICE_PROVISIONING, EndPoint, {}, Body, 120000);
			CallStatus =
				API.Do(CallResponse, client == nullptr ? "" : client->UserInfo_.webtoken.access_token_);
			if (CallStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				return false;
			}
			return device.from_json(CallResponse);
		}

		bool DeleteSubscriberDevice(RESTAPIHandler *client, const std::string &SerialNumber,
									Poco::Net::HTTPServerResponse::HTTPStatus &CallStatus, Poco::JSON::Object::Ptr &CallResponse) {
			std::string EndPoint = "/api/v1/subscriberDevice/" + SerialNumber;
			auto API = OpenAPIRequestDelete(uSERVICE_PROVISIONING, EndPoint, {}, 60000);
			CallStatus = API.Do(CallResponse, client ? client->UserInfo_.webtoken.access_token_ : "");
			if (CallStatus != Poco::Net::HTTPResponse::HTTP_OK) {
				Poco::Logger::get("SDK_prov").error(fmt::format("Failed to delete device [{}] from provisioning subdevice table ", SerialNumber));
			}
			return CallStatus == Poco::Net::HTTPResponse::HTTP_OK;
		}
	} // namespace Subscriber
} // namespace OpenWifi::SDK::Prov
