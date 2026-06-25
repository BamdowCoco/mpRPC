#include "mprpc_channel.h"
#include "rpcheader.pb.h"
#include "mprpc_application.h"
#include "logger.h"
#include "zk_client_util.h"

#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <string>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

// 重写CallMethod
// 所有stub代理对象调用rpc方法都会调用该函数
// 统一做rpc请求序列化、网络发送、接收响应、rpc响应反序列化
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                              google::protobuf::RpcController* controller,
                              const google::protobuf::Message* request,
                              google::protobuf::Message* response, google::protobuf::Closure* done)
{ 
    // headerSize
    // header: serviceName+methodName+argsSize
    // args: request序列化
    
    // request序列化
    std::string argsStr = request->SerializeAsString();
    
    // 定义rpc请求header
    std::string serviceName(method->service()->name());
    std::string methodName(method->name());

    mprpc::RpcHeader header;
    header.set_service_name(serviceName);
    header.set_method_name(methodName);
    header.set_args_size(argsStr.size());
    std::string headerStr = header.SerializeAsString();

    int32_t headerSize = headerStr.size();

    // 组装待发送的rpc请求字符串
    std::string sendRpcStr;
    sendRpcStr.append((char*)&headerSize, 4);
    sendRpcStr.append(headerStr);
    sendRpcStr.append(argsStr);

    LOG("headerSize:" + std::to_string(headerSize));
    LOG("service_name:" + serviceName);
    LOG("method_name:" + methodName);
    LOG("headerStr:" + headerStr);
    LOG("argsStr:" + argsStr);

    // char buffer[1024] = {0};
    // memcpy(buffer, &headerSize, sizeof(headerSize));
    // char* p = buffer+sizeof(headerSize);
    // snprintf(p, sizeof(buffer)-sizeof(headerSize), "%s%s", headerStr.c_str(), argsStr.c_str());
    
    // LOG("headerSize:" + std::to_string(headerSize));
    // LOG("headerStr:" + std::string(headerStr.c_str()));
    // LOG("buffer:" + std::string(buffer));


    // TCP 发送rpc请求
    // 创建套接字 -> 发起连接请求 -> 发送数据 -> 接收响应数据 -> 断开连接
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == fd) {
        std::string reason = "failed to create socket! errno: " + std::to_string(errno);
        LOG(reason);
        controller->SetFailed(reason);
        return;
    }

    // 向zk获取对应服务下方法节点的 ip 和 端口
    ZKClient zkClient;
    zkClient.start();
    std::string methodPath = '/' + serviceName + '/' + methodName; 
    /* 旧版本: 一个方法只能由一个服务器提供 */ 
    // std::string hostStr = zkClient.getData(methodPath);
    // if (hostStr.empty()) {
    //     controller->SetFailed("method:" + methodPath + " doesn't exist!");
    //     return;
    // }

    /* 新版本: 一个方法能由多个服务器提供 */ 
    std::vector<std::string> hostStrVec = zkClient.getChildren(methodPath);
    std::string ip;
    uint16_t port;
    bool isConnSuccess = false;
    for(std::string& hostStr : hostStrVec){
        size_t colonIdx = hostStr.find(':');
        if(colonIdx == std::string::npos) {
            controller->SetFailed("method:" + methodPath + " address invalid!");
            continue;
        }
        ip = hostStr.substr(0, colonIdx);
        port = std::stoi(hostStr.substr(colonIdx+1));

        struct sockaddr_in rpcServerAddr;
        rpcServerAddr.sin_family = AF_INET;
        rpcServerAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &rpcServerAddr.sin_addr.s_addr);
        // rpcServerAddr.sin_port = htons(std::stoi(MprpcApplication::getConfig().load("rpc_server_port")));
        // inet_pton(AF_INET, MprpcApplication::getConfig().load("rpc_server_ip").c_str(), &rpcServerAddr.sin_addr.s_addr);
        // rpcServerAddr.sin_addr.s_addr
        
        if(0 == connect(fd, (struct sockaddr*)&rpcServerAddr, sizeof(rpcServerAddr))) {
            // 连接成功 退出循环
            isConnSuccess = true;
            break;
        }
    }

    if (isConnSuccess) {
        // 连接成功
        LOG_INFO("connect success! ip:%s port:%d", ip.c_str(), port);
    } else {
        // 连接失败
        std::string reason = "failed to connect rpc server! errno: " + std::to_string(errno);
        LOG(reason);
        controller->SetFailed(reason);
        close(fd);
        return;
    }

    if(0 >= send(fd, sendRpcStr.c_str(), sendRpcStr.size(), 0)) {
        std::string reason = "failed to send rpc request! errno: " + std::to_string(errno);
        LOG(reason);
        controller->SetFailed(reason);
        close(fd);
        return;
    }

    // 接收rpc响应
    char responseBuf[1024] = {0};
    int recvSize = 0;
    if(0 >=(recvSize = recv(fd, responseBuf, sizeof(responseBuf), 0)))
    {
        std::string reason = "failed to receive rpc response! errno: " + std::to_string(errno);
        LOG(reason);
        controller->SetFailed(reason);
        close(fd);
        return;
    }

    // 断开连接
    close(fd);

    LOG("recvSize:" + std::to_string(recvSize));
    
    // rpc响应反序列化
    if(!response->ParseFromArray(responseBuf, recvSize)) {
        std::string reason = "failed to parseFromString rpc response! content:" + std::string(responseBuf, recvSize);
        LOG(reason);
        controller->SetFailed(reason);
        return;
    }

}