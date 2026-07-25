#include "mprpc_application.h"
#include "friend.pb.h"
#include "logger.h"

#include <string>

void execGetFriendListRpc(fixbug::FriendRpcService_Stub& stub)
{
    // 创建controller
    MprpcController controller;

    // 创建request
    fixbug::GetFriendListRequest request;
    request.set_id(666);
    // 初始化response
    fixbug::GetFriendListResponse response;
    // response.mutable_result()->set_errcode(-1);
    // 请求调用rpc
    stub.GetFriendList(&controller, &request, &response, nullptr);

    // 查看结果
    if(!controller.Failed()) {
        // rpc方法调用完成 成功接收解析得到rpc响应
        if(0 == response.result().errcode()) {
            LOG_INFO("exec GetFriendList rpc success!");
            for(int i=0;i<response.friend_list_size();i++) {
                const fixbug::User& user = response.friend_list().at(i);
                LOG_INFO("i:%d id:%d name:%s", i, user.id(), user.name().c_str());
            }        
        } else {
            LOG_ERROR("failed to exec GetFriendList rpc! errcode:%d errmsg:%s", response.result().errcode(), response.result().errmsg().c_str());
            
        }
    } else {
        LOG_ERROR("%s",controller.ErrorText().c_str());
    }
    
}

int main(int argc, char** argv) {
    // 框架初始化
    MprpcApplication::init(argc, argv);

    // 创建channel 重写了CallMethod
    MprpcChannel channel;
    // 创建stub
    fixbug::FriendRpcService_Stub stub(&channel);

    execGetFriendListRpc(stub);
    
    return 0;
}