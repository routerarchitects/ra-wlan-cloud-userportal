/*
 * SPDX-License-Identifier: AGPL-3.0 OR LicenseRef-Commercial
 * Copyright (c) 2025 Infernet Systems Pvt Ltd
 * Portions copyright (c) Telecom Infra Project (TIP), BSD-3-Clause
 */

#include "RESTAPI_subscriber_devices_handler.h"
#include "framework/ow_constants.h"
#include "Poco/String.h"

#include "ConfigMaker.h"
#include "framework/utils.h"
#include "sdks/SDK_gw.h"
#include "sdks/SDK_prov.h"

namespace OpenWifi {

	struct AddDeviceContext {
		std::string Mac;
		std::string DeviceName;
		std::string DeviceGroup;
		ProvObjects::SubscriberDeviceList ExistingDevices{};
		ProvObjects::SubscriberDevice SubDevice{};
	};

	bool RESTAPI_subscriber_devices_handler::Validate_Inputs(std::string &mac) {
		if (UserInfo_.userinfo.id.empty()) {
			Logger().error("Subscriber id is missing.");
			UnAuthorized(RESTAPI::Errors::InvalidSubscriberId);
			return false;
		}
		mac = GetBinding("mac", "");
		if (!Utils::NormalizeMac(mac)) {
			Logger().error(fmt::format("Invalid MAC: [{}]", mac));
			BadRequest(RESTAPI::Errors::InvalidMacAddress);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::FetchSubscriberDevices(AddDeviceContext &ctx) {
		Poco::Net::HTTPServerResponse::HTTPStatus status =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		auto response = Poco::makeShared<Poco::JSON::Object>();
		if (SDK::Prov::Subscriber::GetDevices(this, UserInfo_.userinfo.id, UserInfo_.userinfo.owner,
											  ctx.ExistingDevices, status, response)) {
			// list loaded
		} else if (status == Poco::Net::HTTPServerResponse::HTTP_NOT_FOUND) {
			ctx.ExistingDevices.subscriberDevices.clear();
		} else {
			Logger().error(
				fmt::format("Failed to fetch subscriber devices for subscriber [{}], status [{}].",
							UserInfo_.userinfo.id, status));
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::IsDeviceAlreadyProvisioned(
		const AddDeviceContext &ctx) {
		for (const auto &device : ctx.ExistingDevices.subscriberDevices) {
			if (device.serialNumber == ctx.Mac) {
				Logger().warning(
					fmt::format("Device [{}] already exists for subscriber [{}].", ctx.Mac,
								UserInfo_.userinfo.id));
				BadRequest(RESTAPI::Errors::SerialNumberAlreadyProvisioned);
				return true;
			}
		}
		return false;
	}

	void RESTAPI_subscriber_devices_handler::InitializeSubscriberDevice(AddDeviceContext &ctx) {
		ctx.DeviceGroup = ctx.ExistingDevices.subscriberDevices.empty() ? "olg" : "ap";
		ctx.DeviceName = fmt::format("Device-{}", ctx.ExistingDevices.subscriberDevices.size() + 1);
		ctx.SubDevice.info.name = ctx.DeviceName;
		ctx.SubDevice.serialNumber = ctx.Mac;
		ctx.SubDevice.subscriberId = UserInfo_.userinfo.id;
		ctx.SubDevice.deviceGroup = ctx.DeviceGroup;
	}

	bool RESTAPI_subscriber_devices_handler::PrepareGatewayConfiguration(AddDeviceContext &ctx) {
		Poco::JSON::Object::Ptr ConfigObj;
		auto ResponseStatus = SDK::GW::Device::GetConfig(this, ctx.Mac, ConfigObj);
		if (ResponseStatus != Poco::Net::HTTPResponse::HTTP_OK) {
			Logger().error(
				fmt::format("Failed to fetch current configuration for device [{}].", ctx.Mac));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}

		ConfigMaker configMaker(Logger());
		if (!configMaker.BuildGatewayConfig(ConfigObj, ctx.Mac, ctx.SubDevice)) {
			Logger().error(
				fmt::format("Failed to prepare gateway configuration for device [{}].", ctx.Mac));
			InternalError(RESTAPI::Errors::ApplyConfigFailed);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::PrepareMeshConfiguration(AddDeviceContext &ctx) {
		std::string GatewayMac;
		for (const auto &device : ctx.ExistingDevices.subscriberDevices) {
			auto group = device.deviceGroup;
			Poco::toLowerInPlace(group);
			if (group == "olg") {
				GatewayMac = device.serialNumber;
				break;
			}
		}

		if (GatewayMac.empty()) {
			Logger().error(fmt::format("No gateway (deviceGroup=olg) found for subscriber [{}].",
									   UserInfo_.userinfo.id));
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}

		Poco::JSON::Object::Ptr config;
		auto status = SDK::GW::Device::GetConfig(this, GatewayMac, config);
		if (status != Poco::Net::HTTPResponse::HTTP_OK) {
			Logger().error(fmt::format("Failed to fetch gateway [{}] configuration for mesh [{}].",
									   GatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::AddDeviceFailed);
			return false;
		}

		ConfigMaker configMaker(Logger());
		if (!configMaker.ValidateConfig(config, GatewayMac, Logger())) {
			Logger().error(
				fmt::format("Gateway [{}] configuration is invalid for mesh onboarding [{}].",
							GatewayMac, ctx.Mac));
			InternalError(RESTAPI::Errors::ConfigBlockInvalid);
			return false;
		}

		Poco::JSON::Object::Ptr meshConfig;
		if (!configMaker.BuildMeshConfig(config, meshConfig)) {
			Logger().error("Failed to build mesh configuration payload.");
			InternalError(RESTAPI::Errors::ConfigurationMustExist);
			return false;
		}

		if (!configMaker.AppendWeightedSections(meshConfig, ctx.SubDevice.configuration)) {
			Logger().error("Failed to prepare mesh provisioning configuration sections.");
			InternalError(RESTAPI::Errors::ConfigurationMustExist);
			return false;
		}
		return true;
	}

	bool RESTAPI_subscriber_devices_handler::CreateProvisioningRecord(
		AddDeviceContext &ctx) {
		if (!SDK::Prov::Subscriber::CreateSubsciberDeviceMinimal(
				this, ctx.DeviceName, ctx.Mac, UserInfo_.userinfo.id, ctx.DeviceGroup,
				ctx.SubDevice.configuration, ctx.SubDevice)) {
			Logger().error(fmt::format(
				"Failed to create subscriberDevice for serial [{}] in provisioning.", ctx.Mac));
			InternalError(RESTAPI::Errors::RecordNotCreated);
			return false;
		}
		return true;
	}

	void RESTAPI_subscriber_devices_handler::DoPost() {
		AddDeviceContext ctx;

		if (!Validate_Inputs(ctx.Mac))
			return;

		Logger().information(
			fmt::format("Processing Add Device [{}] for subscriber [{}].", ctx.Mac,
						UserInfo_.userinfo.id));

		if (!FetchSubscriberDevices(ctx))
			return;
		if (IsDeviceAlreadyProvisioned(ctx))
			return;

		InitializeSubscriberDevice(ctx);

		if (ctx.DeviceGroup == "olg") {
			if (!PrepareGatewayConfiguration(ctx))
				return;
		} else {
			if (!PrepareMeshConfiguration(ctx))
				return;
		}

		if (!CreateProvisioningRecord(ctx))
			return;

		return OK();
	}

	void RESTAPI_subscriber_devices_handler::DoDelete() {
		std::string Mac;
		if (!Validate_Inputs(Mac))
			return;

		Logger().information(
			fmt::format("Processing Delete Device [{}] request for subscriber: [{}].", Mac,
						UserInfo_.userinfo.id));

		Poco::Net::HTTPServerResponse::HTTPStatus callStatus =
			Poco::Net::HTTPServerResponse::HTTP_INTERNAL_SERVER_ERROR;
		if (!SDK::Prov::Subscriber::DeleteSubscriberDevice(this, Mac, callStatus)) {
			return ForwardErrorResponse(this, callStatus, Poco::makeShared<Poco::JSON::Object>());
		}

		return OK();
	}
} // namespace OpenWifi
