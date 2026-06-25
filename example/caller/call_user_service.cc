#include "mprpc_application.h"
#include "mprpc_channel.h"
#include "user.pb.h"
#include "logger.h"
#include <string>

void execLoginRpc(fixbug::UserServiceRpc_Stub& stub) {
    // rpc请求参数
    fixbug::LoginRequest request;
    request.set_name("zhang_san");
    request.set_pwd("123456");
    // rpc响应
    fixbug::LoginResponse response;
    response.mutable_result()->set_errcode(-1);
    
    // rpc方法调用: 请求序列化、发送请求、接收响应、响应反序列化
    stub.Login(nullptr, &request, &response, nullptr);

    // 一次rpc调用完成 读取rpc响应
    if(response.result().errcode() == 0) {
        // 执行成功
        LOG("execute login Rpc success! response:" + std::to_string(response.success()));
    } else {
        // 执行失败
        LOG("failed to execute login Rpc error:" + response.result().errmsg());
    }
}

void execRegisterRpc(fixbug::UserServiceRpc_Stub& stub) {
    // 创建RegisterRequest
    fixbug::RegisterResquest request;
    request.set_name("LiSi");
    request.set_pwd("999999");
    // 初始化RegisterResponse
    fixbug::RegisterResponse response;
    response.mutable_result()->set_errcode(-1);
    // 请求调用RegisterRpc方法
    stub.Register(nullptr, &request, &response, nullptr);
    // 查看响应结果
    if(0 == response.result().errcode()) {
        LOG("exec  register rpc success! response: id=" + std::to_string(response.id()));
    } else {
        LOG("failed to exec register rpc! errcode:" + std::to_string(response.result().errcode()) + " errmsg:" + response.result().errmsg());
    }
}

int main(int argc, char** argv) {
    // 初始化mprpc框架
    MprpcApplication::init(argc, argv);

    // 演示调用调用远程发布的rpc方法 Login
    MprpcChannel channel;
    fixbug::UserServiceRpc_Stub stub(&channel);

    // execLoginRpc(stub);
    execRegisterRpc(stub);
    

    return 0;
}