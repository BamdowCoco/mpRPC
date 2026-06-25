# file(
#     GLOB SRC_LIST
#     CONFIGURE_DEPENDS
#     # ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
#     ${CMAKE_CURRENT_SOURCE_DIR}/../*.cc
# )

# list(APPEND SRC_LIST friend_service.cc)

set(
    SRC_LIST
    # user_service.cc
    friend_service.cc
    ../user.pb.cc
    ../friend.pb.cc
)

add_executable(callee ${SRC_LIST})

target_link_libraries(
    callee
    mprpc
    protobuf::libprotobuf
)