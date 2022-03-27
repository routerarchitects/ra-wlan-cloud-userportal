cmake_minimum_required(VERSION 3.13)
project(owsub VERSION 2.6.0)

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
    file(READ ${CMAKE_CURRENT_SOURCE_DIR}/build BUILD_NUM)
    if(BUILD_INCREMENT)
        MATH(EXPR BUILD_NUM "${BUILD_NUM}+1")
        file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/build ${BUILD_NUM})
    endif()
else()
    set(BUILD_NUM 1)
    file(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/build ${BUILD_NUM})
endif()

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --tags
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            RESULT_VARIABLE GIT_RESULT
            OUTPUT_VARIABLE GIT_HASH)
    if(NOT GIT_RESULT EQUAL "0")
        message(FATAL_ERROR "git describe --always --tags failed with ${GIT_RESULT}")
    endif()
    string(REGEX REPLACE "\n$" "" GIT_HASH "${GIT_HASH}")
endif()
add_definitions(-DAWS_CUSTOM_MEMORY_MANAGEMENT)

set(BUILD_SHARED_LIBS 1)

find_package(OpenSSL    REQUIRED)
find_package(fmt    REQUIRED)
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

configure_file(src/ow_version.h.in ${PROJECT_SOURCE_DIR}/src/ow_version.h @ONLY)

add_compile_options(-Wall -Wextra)
if(ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

add_executable(owsub
        build
        src/ow_version.h.in
        src/framework/CountryCodes.h
        src/framework/KafkaTopics.h
        src/framework/MicroService.h
        src/framework/OpenWifiTypes.h
        src/framework/orm.h
        src/framework/StorageClass.h
        src/framework/API_Proxy.h
        src/framework/ow_constants.h
        src/RESTObjects/RESTAPI_SecurityObjects.h src/RESTObjects/RESTAPI_SecurityObjects.cpp
        src/RESTObjects/RESTAPI_ProvObjects.cpp src/RESTObjects/RESTAPI_ProvObjects.h
        src/RESTObjects/RESTAPI_GWobjects.h src/RESTObjects/RESTAPI_GWobjects.cpp
        src/RESTObjects/RESTAPI_FMSObjects.h src/RESTObjects/RESTAPI_FMSObjects.cpp
        src/RESTAPI/RESTAPI_routers.cpp
        src/Daemon.cpp src/Daemon.h
        src/Dashboard.h src/Dashboard.cpp
        src/StorageService.cpp src/StorageService.h
        src/SubscriberCache.cpp src/SubscriberCache.h
        src/ConfigMaker.cpp src/ConfigMaker.h
        src/storage/storage_subscriber_info.cpp src/storage/storage_subscriber_info.h
        src/RESTAPI/RESTAPI_wiredClients_handler.cpp src/RESTAPI/RESTAPI_wiredClients_handler.h
        src/RESTAPI/RESTAPI_wifiClients_handler.cpp src/RESTAPI/RESTAPI_wifiClients_handler.h
        src/RESTObjects/RESTAPI_SubObjects.cpp src/RESTObjects/RESTAPI_SubObjects.h
        src/RESTAPI/RESTAPI_subscriber_handler.cpp src/RESTAPI/RESTAPI_subscriber_handler.h
        src/RESTAPI/RESTAPI_oauth2_handler.cpp src/RESTAPI/RESTAPI_oauth2_handler.h
        src/RESTAPI/RESTAPI_action_handler.cpp src/RESTAPI/RESTAPI_action_handler.h
        src/RESTAPI/RESTAPI_mfa_handler.cpp src/RESTAPI/RESTAPI_mfa_handler.h
        src/sdks/SDK_prov.cpp src/sdks/SDK_prov.h
        src/sdks/SDK_gw.cpp src/sdks/SDK_gw.h
        src/sdks/SDK_sec.cpp src/sdks/SDK_sec.h
        src/RESTAPI/RESTAPI_claim_handler.cpp src/RESTAPI/RESTAPI_claim_handler.h
        src/RESTAPI/RESTAPI_signup_handler.cpp src/RESTAPI/RESTAPI_signup_handler.h src/sdks/SDK_fms.cpp src/sdks/SDK_fms.h src/StatsSvr.cpp src/StatsSvr.h src/RESTAPI/RESTAPI_stats_handler.cpp src/RESTAPI/RESTAPI_stats_handler.h)

target_link_libraries(owsub PUBLIC
        ${Poco_LIBRARIES}
        ${MySQL_LIBRARIES}
        ${ZLIB_LIBRARIES}
        ${AWSSDK_LINK_LIBRARIES}
        fmt::fmt
        CppKafka::cppkafka nlohmann_json_schema_validator)

