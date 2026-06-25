# file(
#     GLOB SRC_LIST
#     CONFIGURE_DEPENDS
#     ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
# )

set(
    SRC_LIST
    mprpc_application.cc
    mprpc_channel.cc
    mprpc_config.cc
    mprpc_controller.cc
    mprpc_provider.cc
    rpcheader.pb.cc
    logger.cc
    zk_client_util.cc
)

add_library(mprpc STATIC ${SRC_LIST})

target_link_libraries(
    mprpc PUBLIC
    zookeeper_mt
    protobuf::libprotobuf 
    muduo_net muduo_base 
    pthread
)