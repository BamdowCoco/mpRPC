#include <iostream>
#include <string>
#include "user.pb.h"
#include "mprpc_application.h"
#include "mprpc_provider.h"

/*
UserService 是本地服务, 提供了两个本地方法: Login 和 GetFriendLists
*/

// fixbug::UserServiceRpc 由rpc服务提供者继承
class UserService : public fixbug::UserServiceRpc
{
public:
    bool Login(const std::string name, const std::string pwd)
    {
        std::cout << "doing local service: Login" << std::endl;
        std::cout << "name:" << name << " pwd:" << pwd << std::endl;
        return true;
    }

    int Register(const std::string name, const std::string pwd)
    {
        std::cout << "doing local service: Register" << std::endl;
        std::cout << "name:" << name << " pwd:" << pwd << std::endl;
        std::cout << "id:" << 666 << std::endl;
        return 666;
    }

    /*重写基类 UserServiceRpc的虚函数
    virtual void Login(::google::protobuf::RpcController* PROTOBUF_NULLABLE controller,
                        const ::fixbug::LoginRequest* PROTOBUF_NONNULL request,
                        ::fixbug::LoginResponse* PROTOBUF_NONNULL response,
                        ::google::protobuf::Closure* PROTOBUF_NULLABLE done);
    框架直接调用如下函数
    1. caller ===> Login(LoginRequest) ===> muduo ===> callee
    2. callee ===> 调用如下函数
    */
    virtual void Login(::google::protobuf::RpcController* controller,
                       const ::fixbug::LoginRequest* request,
                       ::fixbug::LoginResponse* response,
                       ::google::protobuf::Closure* done)
        override
    {
        // 框架上报了请求参数 LoginRequest, 运行本地业务
        bool loginResult = Login(request->name(), request->pwd());
        
        // 填写响应 包括错误码、错误信息、返回值
        ::fixbug::ResultCode* code =  response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(loginResult);
        
        // 执行回调操作 即进行响应序列化和网络发送(由框架实现完成)
        done->Run();

    }

    virtual void Register(::google::protobuf::RpcController* controller,
                        const ::fixbug::RegisterResquest* request,
                        ::fixbug::RegisterResponse* response,
                        ::google::protobuf::Closure* done)
        override
    {
        // 执行本地方法
        int id = Register(request->name(), request->pwd());
        // 写入rpc响应
        ::fixbug::ResultCode* code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_id(id);
        // 执行回调
        done->Run();
    }
};

int main(int argc, char** argv) {
    // 框架初始化
    MprpcApplication::init(argc, argv);
    // MprpcApplication::init(argc, argv);

    // Rpcprovider是一个网络服务对象
    // 发布服务到rpc节点
    RpcProvider provider;
    provider.notifyService(new UserService());

    // 启动一个rpc服务节点
    // 进入阻塞态, 等待rpc远程过程调用请求
    provider.run();

    return 0;
}