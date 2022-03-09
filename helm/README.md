# owsub

This Helm chart helps to deploy OpenWIFI Subscription service (further on refered as __Subscription__) to the Kubernetes clusters. It is mainly used in [assembly chart](https://github.com/Telecominfraproject/wlan-cloud-ucentral-deploy/tree/main/chart) as Subscription service requires other services as dependencies that are considered in that Helm chart. This chart is purposed to define deployment logic close to the application code itself and define default values that could be overriden during deployment.


## TL;DR;

```bash
$ helm install .
```

## Introduction

This chart bootstraps the Subscription on a [Kubernetes](http://kubernetes.io) cluster using the [Helm](https://helm.sh) package manager.

## Installing the Chart

Currently this chart is not assembled in charts archives, so [helm-git](https://github.com/aslafy-z/helm-git) is required for remote the installation

To install the chart with the release name `my-release`:

```bash
$ helm install --name my-release git+https://github.com/Telecominfraproject/wlan-cloud-owsub@helm/owsub-0.1.0.tgz?ref=main
```

The command deploys the Subscription on the Kubernetes cluster in the default configuration. The [configuration](#configuration) section lists the parameters that can be configured during installation.

> **Tip**: List all releases using `helm list`

## Uninstalling the Chart

To uninstall/delete the `my-release` deployment:

```bash
$ helm delete my-release
```

The command removes all the Kubernetes components associated with the chart and deletes the release.

## Configuration

The following table lists the configurable parameters of the chart and their default values. If Default value is not listed in the table, please refer to the [Values](values.yaml) files for details.

| Parameter | Type | Description | Default |
|-----------|------|-------------|---------|
| replicaCount | number | Amount of replicas to be deployed | `1` |
| strategyType | string | Application deployment strategy | `'Recreate'` |
| nameOverride | string | Override to be used for application deployment |  |
| fullnameOverride | string | Override to be used for application deployment (has priority over nameOverride) |  |
| images.owsub.repository | string | Docker image repository |  |
| images.owsub.tag | string | Docker image tag | `'master'` |
| images.owsub.pullPolicy | string | Docker image pull policy | `'Always'` |
| services.owsub.type | string | OpenWIFI Subscription service type | `'LoadBalancer'` |
| services.owsub.ports.restapi.servicePort | number | REST API endpoint port to be exposed on service | `16001` |
| services.owsub.ports.restapi.targetPort | number | REST API endpoint port to be targeted by service | `16001` |
| services.owsub.ports.restapi.protocol | string | REST API endpoint protocol | `'TCP'` |
| services.owsub.ports.restapiinternal.servicePort | string | Internal REST API endpoint port to be exposed on service | `17001` |
| services.owsub.ports.restapiinternal.targetPort | number | Internal REST API endpoint port to be targeted by service | `17001` |
| services.owsub.ports.restapiinternal.protocol | string | Internal REST API endpoint protocol | `'TCP'` |
| checks.owsub.liveness.httpGet.path | string | Liveness check path to be used | `'/'` |
| checks.owsub.liveness.httpGet.port | number | Liveness check port to be used (should be pointint to ALB endpoint) | `16101` |
| checks.owsub.readiness.httpGet.path | string | Readiness check path to be used | `'/'` |
| checks.owsub.readiness.httpGet.port | number | Readiness check port to be used (should be pointint to ALB endpoint) | `16101` |
| ingresses.restapi.enabled | boolean | Defines if REST API endpoint should be exposed via Ingress controller | `False` |
| ingresses.restapi.hosts | array | List of hosts for exposed REST API |  |
| ingresses.restapi.paths | array | List of paths to be exposed for REST API |  |
| volumes.owsub | array | Defines list of volumes to be attached to the Subscription |  |
| persistence.enabled | boolean | Defines if the Subscription requires Persistent Volume (required for permanent files storage and SQLite DB if enabled) | `True` |
| persistence.accessModes | array | Defines PV access modes |  |
| persistence.size | string | Defines PV size | `'10Gi'` |
| public_env_variables | hash | Defines list of environment variables to be passed to the Subscription | |
| configProperties | hash | Configuration properties that should be passed to the application in `owsub.properties`. May be passed by key in set (i.e. `configProperties."rtty\.token"`) | |
| certs | hash | Defines files (keys and certificates) that should be passed to the Subscription (PEM format is adviced to be used) (see `volumes.owsub` on where it is mounted) |  |


Specify each parameter using the `--set key=value[,key=value]` argument to `helm install`. For example,

```bash
$ helm install --name my-release \
  --set replicaCount=1 \
    .
```

The above command sets that only 1 instance of your app should be running

Alternatively, a YAML file that specifies the values for the parameters can be provided while installing the chart. For example,

```bash
$ helm install --name my-release -f values.yaml .
```

> **Tip**: You can use the default [values.yaml](values.yaml) as a base for customization.


