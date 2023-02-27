<p align="center">
    <img src="images/project/logo.svg" width="200"/>
</p>

# OpenWifi User Self-Care portal (OWSUB)

## What is it?
The OWSUB is a service for the TIP OpenWiFi CloudSDK (OWSDK).
OWSUB provides a subscriber management service to the Subscriber App and interacts with the rest of the Cloud SDK. OWSUB, 
like all other OWSDK microservices, is defined using an OpenAPI definition and uses the ucentral communication 
protocol to interact with Access Points. To use the OWSUB, you either need to [build it](#building) or use the 
[Docker version](#docker).

## OpenAPI
You may get static page with OpenAPI docs generated from the definition on [GitHub Page](https://telecominfraproject.github.io/wlan-cloud-userportal/).

## Building
To build the microservice from source, please follow the instructions in [here](./BUILDING.md)

## Docker
To use the CLoudSDK deployment please follow [here](https://github.com/Telecominfraproject/wlan-cloud-ucentral-deploy)
Also, you may use [Swagger UI](https://petstore.swagger.io/#/) with OpenAPI definition file raw link (i.e. [latest version file](https://raw.githubusercontent.com/Telecominfraproject/wlan-cloud-userportal/main/openapi/owanalytics.yaml)) to get interactive docs page.

#### Expected directory layout
From the directory where your cloned source is, you will need to create the `certs`, `logs`, and `uploads` directories.
```bash
mkdir certs
mkdir certs/cas
mkdir logs
mkdir uploads
```
You should now have the following:
```text
--+-- certs
  |   +--- cas
  +-- cmake
  +-- cmake-build
  +-- logs
  +-- src
  +-- test_scripts
  +-- openapi
  +-- uploads
  +-- owsub.properties
```

### Certificate
The OWSUB uses a certificate to provide security for the REST API Certificate to secure the Northbound API.

#### The `certs` directory
For all deployments, you will need the following `certs` directory, populated with the proper files.

```text
certs ---+--- restapi-ca.pem
         +--- restapi-cert.pem
         +--- restapi-key.pem
```

## Firewall Considerations
| Port  | Description                                 | Configurable |
|:------|:--------------------------------------------|:------------:|
| 16006 | Default port for REST API Access to the OWSUB |     yes      |

### Environment variables
The following environment variables should be set from the root directory of the service. They tell the OWSUB process where to find
the configuration and the root directory.
```bash
export OWSUB_ROOT=`pwd`
export OWSUB_CONFIG=`pwd`
```
You can run the shell script `set_env.sh` from the microservice root.

### OWSUB Service Configuration
The configuration is kept in a file called `owsub.properties`. To understand the content of this file,
please look [here](https://github.com/Telecominfraproject/wlan-cloud-userportal/blob/main/CONFIGURATION.md)

## Kafka topics
Toe read more about Kafka, follow the [document](https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/main/KAFKA.md)

## Contributions
We need more contributors. Should you wish to contribute,
please follow the [contributions](https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/CONTRIBUTING.md) document.

## Pull Requests
Please create a branch with the Jira addressing the issue you are fixing or the feature you are implementing.
Create a pull-request from the branch into master.

## Additional OWSDK Microservices
Here is a list of additional OWSDK microservices
| Name | Description | Link | OpenAPI |
| :--- | :--- | :---: | :---: |
| OWSEC | Security Service | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralsec) | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralsec/blob/main/openpapi/owsec.yaml) |
| OWGW | Controller Service | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralgw) | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralgw/blob/master/openapi/owgw.yaml) |
| OWFMS | Firmware Management Service | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralfms) | [here](https://github.com/Telecominfraproject/wlan-cloud-ucentralfms/blob/main/openapi/owfms.yaml) |
| OWPROV | Provisioning Service | [here](https://github.com/Telecominfraproject/wlan-cloud-owprov) | [here](https://github.com/Telecominfraproject/wlan-cloud-owprov/blob/main/openapi/owprov.yaml) |
| OWANALYTICS | Analytics Service | [here](https://github.com/Telecominfraproject/wlan-cloud-analytics) | [here](https://github.com/Telecominfraproject/wlan-cloud-analytics/blob/main/openapi/owanalytics.yaml) |
| OWSUB | Subscriber Service | [here](https://github.com/Telecominfraproject/wlan-cloud-userportal) | [here](https://github.com/Telecominfraproject/wlan-cloud-userportal/blob/main/openapi/userportal.yaml) |
