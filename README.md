<p align="center">
  <img src="images/project/logo.svg" height="170" align="middle" alt="TIP OpenWiFi Logo" />
  &nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;
  <img src="images/project/mango-logo.png" height="90" align="middle" alt="Mango Cloud Logo" />
</p>

# OpenWiFi User Self-Care Portal (OWSUB)

## Overview
The OpenWiFi User Self-Care Portal (OWSUB) is a core service within the Telecom Infra Project (TIP) OpenWiFi CloudSDK (OWSDK) ecosystem.

OWSUB provides a subscriber management service to the Subscriber Self-Care App and Web Portal. Like all other OWSDK microservices, OWSUB is defined using an OpenAPI definition and integrates with other CloudSDK services (such as the Security Service `owsec` and Provisioning Service `owprov`) to support subscriber-specific device provisioning and workflows. To use OWSUB, you can either [build it from source](#building) or deploy the containerized version using [Docker](#docker).

## Role in Mango Cloud
This service is part of [Mango Cloud](https://www.mangowifi.cloud/), Router Architects’ open-source platform for managed Wi-Fi and connectivity operations.

Within Mango Cloud, **OWSUB** serves as the **Subscriber Portal Service** (backend node `owsub`).

Key integrations include:
* **Subscriber Self-Care API**: Exposes the REST API (defaulting to port `16006`) used by the Subscriber Mobile App and Web Portal to authenticate users, manage personal passwords, and monitor active subscriptions.
* **Device Control Options**: Connects with the Provisioning Service (`owprov`) to associate subscriber accounts with specific hardware (gateways and mesh nodes) and automatically provision subscriber-specific SSIDs.
* **Signup Flow**: Manages the verification templates and database records for subscriber signups, credentials, and verification updates.

### Resources
* [Mango Cloud Website](https://www.mangowifi.cloud/)
* [Mango Cloud Deployment Guide](https://github.com/routerarchitects/mango-cloud-deployment)
* [Router Architects GitHub Organization](https://github.com/routerarchitects)

### Subscriber Guides
* [Subscriber Onboarding & Portal Settings](https://www.mangowifi.cloud/docs/operations/provisioning-hierarchy-owprov/subscribers)

## OpenAPI
The OWSUB REST API is defined in the OpenAPI specification [openapi/userportal.yaml](https://raw.githubusercontent.com/routerarchitects/ra-wlan-cloud-userportal/refs/heads/main/openapi/userportal.yaml). You can use this OpenAPI definition to inspect endpoints, generate client SDKs, or build static documentation.

## Building
To build the microservice from source, please follow the instructions in [BUILDING.md](./BUILDING.md).

## Docker
To use the containerized CloudSDK deployment, please refer to the deployment guide in the [mango-cloud-deployment](https://github.com/routerarchitects/mango-cloud-deployment) repository.

## Firewall Considerations
Depending on your deployment, ensure that firewalls allow traffic on the following ports:

| Port  | Service Type / Description | Configurable |
|:------|:--------------------------------------------|:------------:|
| **16006** | Public REST API Access for subscriber portals | yes |
| **17006** | Internal REST API Access for intra-microservice communication | yes |
| **16106** | Application Load Balancer (ALB) health check endpoint | yes |

### OWSUB Service Configuration
The configuration is kept in a file called `owsub.properties`. To understand the configuration details, please see [CONFIGURATION.md](https://github.com/routerarchitects/ra-wlan-cloud-userportal/blob/main/CONFIGURATION.md).

## Kafka topics
To read more about Kafka integration across microservices, follow the [wlan-cloud-ucentralgw Kafka Documentation](https://github.com/routerarchitects/ra-wlan-cloud-ucentralgw/blob/main/KAFKA.md).
