cmake_minimum_required(VERSION 3.21)

project(lwm2mclient_mbedtls C)

set(SMGW_C_REPO_ROOT
    "$ENV{SMGW_C_REPO_ROOT}"
    CACHE PATH "SMGW-C repository root"
)
if(NOT SMGW_C_REPO_ROOT)
    get_filename_component(SMGW_C_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)
endif()
set(SMGW_IDENTITY_DIRECTORY "${SMGW_C_REPO_ROOT}/shared/identity")
set(SMGW_CREDENTIAL_DIRECTORY "${SMGW_C_REPO_ROOT}/shared/credential")
set(SMGW_CONFIG_DIRECTORY "${SMGW_C_REPO_ROOT}/shared/config")

set(WAKAAMA_MODE_CLIENT ON)
set(WAKAAMA_MODE_SERVER OFF)
set(WAKAAMA_MODE_BOOTSTRAP_SERVER OFF)
set(WAKAAMA_CLIENT_INITIATED_BOOTSTRAP ON)
set(WAKAAMA_DATA_SENML_JSON ON)
set(WAKAAMA_DATA_SENML_CBOR OFF)
set(WAKAAMA_CLI ON)
set(WAKAAMA_TRANSPORT MBEDTLS)
set(WAKAAMA_PLATFORM POSIX)
add_subdirectory(../../../ wakaama_lib)

if(TARGET wakaama_transport_posix_mbedtls)
    target_include_directories(wakaama_transport_posix_mbedtls PRIVATE ${SMGW_CONFIG_DIRECTORY})
endif()

add_executable(lwm2mclient_mbedtls)

target_sources(
    lwm2mclient_mbedtls
    PRIVATE ../common/lwm2mclient.c
            ../common/lwm2mclient.h
            ../common/object_access_control.c
            ../common/object_connectivity_moni.c
            ../common/object_connectivity_stat.c
            ../common/object_device.c
            ../common/object_firmware.c
            ../common/object_location.c
            ../common/object_security.c
            ../common/object_server.c
            ../common/object_test.c
            ../common/system_api.c
            ${SMGW_CONFIG_DIRECTORY}/smgw_lwm2m_network.c
            ${SMGW_CREDENTIAL_DIRECTORY}/smgw_lwm2m_credential.c
            ${SMGW_IDENTITY_DIRECTORY}/smgw_identity.c
)

target_include_directories(lwm2mclient_mbedtls PRIVATE ${SMGW_CONFIG_DIRECTORY} ${SMGW_CREDENTIAL_DIRECTORY} ${SMGW_IDENTITY_DIRECTORY})
target_link_libraries(lwm2mclient_mbedtls PRIVATE wakaama_static)

enable_testing()
