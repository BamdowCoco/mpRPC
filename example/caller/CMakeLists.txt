# file(
#     GLOB SRC_LIST
#     CONFIGURE_DEPENDS
#     # ${CMAKE_CURRENT_SOURCE_DIR}/*.cc
#     ${CMAKE_CURRENT_SOURCE_DIR}/../*.cc
# )

# list(APPEND SRC_LIST call_friend_service.cc)

set(
    SRC_LIST
    # call_user_service.cc
    call_friend_service.cc
    ../user.pb.cc
    ../friend.pb.cc
)

add_executable(caller ${SRC_LIST})

target_link_libraries(
    caller
    mprpc
    protobuf::libprotobuf
)