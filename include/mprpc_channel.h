#pragma once

#include <google/protobuf/service.h>

class MprpcChannel: public google::protobuf::RpcChannel
{
public:
    MprpcChannel() {}

    // 重写CallMethod
    // 所有stub代理对象调用rpc方法都会调用该函数
    // 统一做rpc请求序列化、网络发送、接收响应、rpc响应反序列化
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                          google::protobuf::RpcController* controller, const google::protobuf::Message* request,
                          google::protobuf::Message* response, google::protobuf::Closure* done) override;    

};