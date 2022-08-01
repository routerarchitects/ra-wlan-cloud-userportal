FROM alpine:3.15 AS build-base

RUN apk add --update --no-cache \
    make cmake g++ git \
    unixodbc-dev postgresql-dev mariadb-dev \
    librdkafka-dev boost-dev openssl-dev \
    zlib-dev nlohmann-json

FROM build-base AS fmtlib-build

ADD https://api.github.com/repos/fmtlib/fmt/git/refs/heads/master version.json
RUN git clone https://github.com/fmtlib/fmt /fmtlib

WORKDIR /fmtlib
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN make
RUN make install

FROM build-base AS poco-build

ADD https://api.github.com/repos/AriliaWireless/poco/git/refs/tags/poco-tip-v1 version.json
RUN git clone https://github.com/AriliaWireless/poco --branch poco-tip-v1 /poco

WORKDIR /poco
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

FROM build-base AS cppkafka-build

ADD https://api.github.com/repos/stephb9959/cppkafka/git/refs/heads/master version.json
RUN git clone https://github.com/stephb9959/cppkafka /cppkafka

WORKDIR /cppkafka
RUN mkdir cmake-build
WORKDIR cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8
RUN cmake --build . --target install

FROM build-base AS json-schema-validator-build

ADD https://api.github.com/repos/pboettch/json-schema-validator/git/refs/heads/master version.json
RUN git clone https://github.com/pboettch/json-schema-validator /json-schema-validator

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
COPY --from=fmtlib-build /usr/local/include /usr/local/include
COPY --from=fmtlib-build /usr/local/lib /usr/local/lib

WORKDIR /owsub
RUN mkdir cmake-build
WORKDIR /owsub/cmake-build
RUN cmake ..
RUN cmake --build . --config Release -j8

FROM alpine:3.15

ENV OWSUB_USER=owsub \
    OWSUB_ROOT=/owsub-data \
    OWSUB_CONFIG=/owsub-data

RUN addgroup -S "$OWSUB_USER" && \
    adduser -S -G "$OWSUB_USER" "$OWSUB_USER"

RUN mkdir /openwifi
RUN mkdir -p "$OWSUB_ROOT" "$OWSUB_CONFIG" && \
    chown "$OWSUB_USER": "$OWSUB_ROOT" "$OWSUB_CONFIG"

RUN apk add --update --no-cache librdkafka su-exec gettext ca-certificates bash jq curl \
    mariadb-connector-c libpq unixodbc postgresql-client

COPY test_scripts/curl/cli /cli

COPY owsub.properties.tmpl /
COPY docker-entrypoint.sh /
COPY wait-for-postgres.sh /

RUN wget https://raw.githubusercontent.com/Telecominfraproject/wlan-cloud-ucentral-deploy/main/docker-compose/certs/restapi-ca.pem \
    -O /usr/local/share/ca-certificates/restapi-ca-selfsigned.pem

COPY --from=owsub-build /owsub/cmake-build/owsub /openwifi/owsub
COPY --from=cppkafka-build /cppkafka/cmake-build/src/lib /usr/local/lib
COPY --from=poco-build /poco/cmake-build/lib /usr/local/lib

EXPOSE 16006 17006 16106

ENTRYPOINT ["/docker-entrypoint.sh"]
CMD ["/openwifi/owsub"]
