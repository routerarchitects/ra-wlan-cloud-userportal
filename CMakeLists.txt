cmake_minimum_required(VERSION 3.13)
project(owsub VERSION 2.2.0)

set(CMAKE_CXX_STANDARD 17)

if(UNIX AND APPLE)
    set(OPENSSL_ROOT_DIR /usr/local/opt/openssl)
    set(MYSQL_ROOT_DIR /usr/local/opt/mysql-client)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
endif()

if(UNIX AND NOT APPLE)
    set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include/postgresql)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/build)
    file(READ build BUILD_NUM)
    if(BUILD_INCREMENT)
        MATH(EXPR BUILD_NUM "${BUILD_NUM}+1")
        file(WRITE build ${BUILD_NUM})
    endif()
else()
    set(BUILD_NUM 1)
    file(WRITE build ${BUILD_NUM})
endif()

set(BUILD_SHARED_LIBS 1)
add_definitions(-DAPP_VERSION="${CMAKE_PROJECT_VERSION}" -DBUILD_NUMBER="${BUILD_NUM}" -DAWS_CUSTOM_MEMORY_MANAGEMENT)

set(Boost_USE_STATIC_LIBS OFF)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_RUNTIME OFF)

find_package(Boost      REQUIRED system)
find_package(OpenSSL    REQUIRED)
find_package(AWSSDK     REQUIRED COMPONENTS s3)
find_package(Poco       REQUIRED COMPONENTS Crypto JWT Net Util NetSSL Data DataSQLite)
find_package(nlohmann_json  REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)

if(SMALL_BUILD)
    find_package(Poco REQUIRED COMPONENTS Crypto JWT Net Util NetSSL Data DataSQLite)
else()
    find_package(CppKafka REQUIRED)
    find_package(PostgreSQL REQUIRED)
    find_package(MySQL REQUIRED)
    find_package(Poco REQUIRED COMPONENTS JSON Crypto JWT Net Util NetSSL Data DataSQLite DataPostgreSQL DataMySQL)
endif()

include_directories(/usr/local/include  /usr/local/opt/openssl/include src include/kafka /usr/local/opt/mysql-client/include)

add_executable(owsub
        build
        src/Daemon.cpp src/Daemon.h
        src/Dashboard.h src/Dashboard.cpp
        src/framework/MicroService.cpp src/framework/MicroService.h
        src/framework/Utils.h src/framework/Utils.cpp
        src/framework/RESTAPI_utils.cpp src/framework/RESTAPI_utils.h
        src/RESTAPI/RESTAPI_SecurityObjects.h src/RESTAPI/RESTAPI_SecurityObjects.h
        src/framework/ALBHealthCheckServer.h
        src/framework/AuthClient.cpp src/framework/AuthClient.h
        src/framework/KafkaManager.cpp src/framework/KafkaManager.h
        src/framework/Kafka_topics.h
        src/framework/OpenAPIRequest.cpp src/framework/OpenAPIRequest.h
        src/RESTAPI/RESTAPI_SecurityObjects.h src/RESTAPI/RESTAPI_SecurityObjects.cpp
        src/framework/RESTAPI_handler.cpp src/framework/RESTAPI_handler.h
        src/RESTAPI/RESTAPI_server.h src/RESTAPI/RESTAPI_server.cpp
        src/framework/RESTAPI_system_command.h src/framework/RESTAPI_system_command.cpp
        src/RESTAPI/RESTAPI_InternalServer.h src/RESTAPI/RESTAPI_InternalServer.cpp
        src/StorageService.cpp src/StorageService.h
        src/framework/SubSystemServer.cpp src/framework/SubSystemServer.h
        src/RESTAPI/RESTAPI_ProvObjects.cpp src/RESTAPI/RESTAPI_ProvObjects.h
        src/framework/OpenWifiTypes.h
        src/framework/storage_setup.cpp
        src/framework/CountryCodes.h)

target_link_libraries(owsub PUBLIC
        ${Poco_LIBRARIES} ${MySQL_LIBRARIES}
        ${Boost_LIBRARIES}
        ${ZLIB_LIBRARIES} ${AWSSDK_LINK_LIBRARIES}
        CppKafka::cppkafka nlohmann_json_schema_validator)

