ARG DEBIAN_VERSION=11.5-slim
ARG POCO_VERSION=poco-tip-v1
ARG CPPKAFKA_VERSION=tip-v1
ARG JSON_VALIDATOR_VERSION=2.1.0

FROM debian:$DEBIAN_VERSION AS build-base

RUN apt-get update && apt-get install --no-install-recommends -y \
    make cmake g++ git \
    libpq-dev libmariadb-dev libmariadbclient-dev-compat \
    librdkafka-dev libboost-all-dev libssl-dev \
    zlib1g-dev nlohmann-json3-dev ca-certificates libfmt-dev

FROM build-base AS poco-build

ARG POCO_VERSION

ADD https://api.github.com/repos/AriliaWireless/poco/git/refs/tags/${POCO_VERSION} version.json
RUN git clone https://github.com/AriliaWireless/poco --branch ${POCO_VERSION} /poco

WORKDIR /poco
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

FROM build-base AS cppkafka-build

ARG CPPKAFKA_VERSION

ADD https://api.github.com/repos/AriliaWireless/cppkafka/git/refs/tags/${CPPKAFKA_VERSION} version.json
RUN git clone https://github.com/AriliaWireless/cppkafka --branch ${CPPKAFKA_VERSION} /cppkafka

WORKDIR /cppkafka
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

FROM build-base AS json-schema-validator-build

ARG JSON_VALIDATOR_VERSION

ADD https://api.github.com/repos/pboettch/json-schema-validator/git/refs/tags/${JSON_VALIDATOR_VERSION} version.json
RUN git clone https://github.com/pboettch/json-schema-validator --branch ${JSON_VALIDATOR_VERSION} /json-schema-validator

WORKDIR /json-schema-validator
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN make
RUN make install

FROM build-base AS owsub-build

ADD CMakeLists.txt build /owsub/
ADD cmake /owsub/cmake
ADD src /owsub/src
ADD .git /owsub/.git

COPY --from=poco-build /usr/local/include /usr/local/include
COPY --from=poco-build /usr/local/lib /usr/local/lib
COPY --from=cppkafka-build /usr/local/include /usr/local/include
COPY --from=cppkafka-build /usr/local/lib /usr/local/lib
COPY --from=json-schema-validator-build /usr/local/include /usr/local/include
COPY --from=json-schema-validator-build /usr/local/lib /usr/local/lib

WORKDIR /owsub
RUN mkdir cmake-build
WORKDIR /owsub/cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8

FROM debian:$DEBIAN_VERSION

ENV OWSUB_USER=owsub \
    OWSUB_ROOT=/owsub-data \
    OWSUB_CONFIG=/owsub-data

RUN useradd "$OWSUB_USER"

RUN mkdir /openwifi
RUN mkdir -p "$OWSUB_ROOT" "$OWSUB_CONFIG" && \
    chown "$OWSUB_USER": "$OWSUB_ROOT" "$OWSUB_CONFIG"

RUN apt-get update && apt-get install --no-install-recommends -y \
    librdkafka++1 gosu gettext ca-certificates bash jq curl wget \
    libmariadb-dev-compat libpq5 postgresql-client libfmt7

COPY test_scripts/curl/cli /cli

COPY owsub.properties.tmpl /
COPY docker-entrypoint.sh /
COPY wait-for-postgres.sh /

RUN wget https://raw.githubusercontent.com/Telecominfraproject/wlan-cloud-ucentral-deploy/main/docker-compose/certs/restapi-ca.pem \
    -O /usr/local/share/ca-certificates/restapi-ca-selfsigned.crt

COPY --from=owsub-build /owsub/cmake-build/owsub /openwifi/owsub
COPY --from=cppkafka-build /cppkafka/cmake-build/src/lib /usr/local/lib/
COPY --from=poco-build /poco/cmake-build/lib /usr/local/lib/

RUN ldconfig

EXPOSE 16006 17006 16106

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["/openwifi/owsub"]
