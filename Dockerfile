FROM alpine AS builder

RUN apk add --update --no-cache \
    openssl openssh \
    ncurses-libs \
    bash util-linux coreutils curl \
    make cmake gcc g++ libstdc++ libgcc git zlib-dev yaml-cpp-dev \
    openssl-dev boost-dev unixodbc-dev postgresql-dev mariadb-dev \
    apache2-utils yaml-dev apr-util-dev \
    lua-dev librdkafka-dev \
    nlohmann-json

RUN git clone https://github.com/telecominfraproject/wlan-cloud-userportal /owsub
RUN git clone https://github.com/stephb9959/poco /poco
RUN git clone https://github.com/stephb9959/cppkafka /cppkafka
RUN git clone https://github.com/pboettch/json-schema-validator /json-schema-validator

WORKDIR /cppkafka
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

WORKDIR /poco
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

WORKDIR /json-schema-validator
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN make
RUN make install

ADD CMakeLists.txt build /owsub/
ADD cmake /owsub/cmake
ADD src /owsub/src
ADD .git /owsub/.git

# Build the service
WORKDIR /owsub
RUN mkdir cmake-build
WORKDIR /owsub/cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8

FROM alpine

ENV OWSUB_USER=owsub \
    OWSUB_ROOT=/owsub-data \
    OWSUB_CONFIG=/owsub-data

RUN addgroup -S "OWSUB_USER" && \
    adduser -S -G "OWSUB_USER" "OWSUB_USER"

RUN mkdir /openwifi
RUN mkdir -p "OWSUB_ROOT" "OWSUB_CONFIG" && \
    chown "OWSUB_USER": "OWSUB_ROOT" "OWSUB_CONFIG"
RUN apk add --update --no-cache librdkafka mariadb-connector-c libpq unixodbc su-exec gettext ca-certificates bash jq curl

COPY --from=builder /owsub/cmake-build/owsub /openwifi/owsub
COPY --from=builder /cppkafka/cmake-build/src/lib/* /lib/
COPY --from=builder /poco/cmake-build/lib/* /lib/

COPY owsub.properties.tmpl /
COPY docker-entrypoint.sh /
COPY wait-for-postgres.sh /

RUN wget https://raw.githubusercontent.com/Telecominfraproject/wlan-cloud-ucentral-deploy/main/docker-compose/certs/restapi-ca.pem \
    -O /usr/local/share/ca-certificates/restapi-ca-selfsigned.pem \

COPY readiness_check /readiness_check
COPY test_scripts/curl/cli /cli

EXPOSE 16006

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["/openwifi/owsub"]
