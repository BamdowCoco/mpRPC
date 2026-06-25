#include <iostream>
#include <vector>

#include "friend.pb.h"
#include "mprpc_application.h"
#include "mprpc_provider.h"

class FriendService: public fixbug::FriendRpcService
{
public:
    std::vector<fixbug::User> GetFriendList(const int id) {
        std::cout << "do local GetFriendList!" << std::endl;
        std::cout << "id: " << id << std::endl;
        std::vector<fixbug::User> friendList;
        fixbug::User user;
        user.set_id(10);
        user.set_name("WangWu");
        friendList.push_back(user);
        user.set_id(24);
        user.set_name("XiBin");
        friendList.push_back(user);
        return friendList;
    }

    // 重写虚函数
    virtual void GetFriendList(::google::protobuf::RpcController* controller,
                        const ::fixbug::GetFriendListRequest* request,
                        ::fixbug::GetFriendListResponse* response,
                        ::google::protobuf::Closure* done)
        override
    {
        // 调用本地方法
        std::vector<fixbug::User> friendList = GetFriendList(request->id());
        // 写入响应
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->mutable_friend_list()->Add(friendList.begin(), friendList.end());
        // 调用回调 相应序列化 发送给客户端
        done->Run();
    }
};

int main(int argc, char** argv) {
    // 框架初始化
    MprpcApplication::init(argc, argv);
    // 注册rpc服务
    RpcProvider provider;
    FriendService friendService;
    provider.notifyService(&friendService);
    
    // 启动rpc节点
    provider.run();
    
    return 0;
}