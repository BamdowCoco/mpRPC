#include "mprpc_provider.h"
#include "logger.h"
#include "mprpc_application.h"
#include "rpcheader.pb.h"
#include "zk_client_util.h"

#include <functional>

/*
service_name => service描述 => Service* 服务对象
服务下的method_name => method方法对象
*/
// 提供给外部 发布rpc方法的接口
void RpcProvider::notifyService(google::protobuf::Service* service)
{
    ServiceInfo serviceInfo;
    serviceInfo.m_service = service;

    // 获取服务对象的描述信息
    const google::protobuf::ServiceDescriptor* serviceDescPtr = service->GetDescriptor();

    // 获取服务的名字
    std::string serviceName(serviceDescPtr->name());
    // std::string serviceFullName(serviceDescPtr->full_name());
    
    LOG_INFO("serviceName: %s", serviceName.c_str());    
    // LOG("serviceName:"+serviceName);
    // LOG("serviceFullName:"+serviceFullName);
    
    // 获取服务对象 方法数量
    int methodCount = serviceDescPtr->method_count();
    LOG_INFO("methodCount: %d", methodCount);
    // LOG("methodCount:"+std::to_string(methodCount));
    for (int i=0; i<methodCount; i++) {
        // 获取服务对象对应下标的服务方法描述信息
        const google::protobuf::MethodDescriptor* methodDescPtr = serviceDescPtr->method(i);
        std::string methodName(methodDescPtr->name());
        LOG_INFO("methodName: %s", methodName.c_str());    
        serviceInfo.m_methodMap.insert({methodName, methodDescPtr});
    }

    m_serviceInfoMap.insert({serviceName, serviceInfo});
    // m_serviceMap.insert({serviceName, service});
}

// 启动rpc服务节点 开始提供rpc远程过程调用网络服务
void RpcProvider::run()
{
    std::string ip = MprpcApplication::getConfig().load("rpc_server_ip");
    uint16_t port = std::stoi(MprpcApplication::getConfig().load("rpc_server_port"));
    muduo::net::InetAddress addr(ip, port);

    // 创建TcpServer对象
    muduo::net::TcpServer server(&m_eventLoop, addr, "RpcProvider");

    // 注册连接回调 和 消息读写回调
    // 分离了网络通信和业务处理模块
    server.setConnectionCallback(std::bind(&RpcProvider::onConnection, this, std::placeholders::_1));
    server.setMessageCallback(std::bind(&RpcProvider::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // 设置专门负责通信的I/O线程池大小 即不包括监听连接的主线程
    server.setThreadNum(4);

    LOG_INFO("RpcProvider start service ip:%s port:%d", ip.c_str(), port);
    // LOG("RpcProvider start service ip:" + ip + " port:" + std::to_string(port));

    // 把当前节点的服务和方法 注册到ZooKeeper上
    ZKClient zkClient;
    zkClient.start();
    for(auto& serviceInfoPair : m_serviceInfoMap) {
        //  /service_name
        std::string servicePath = '/'+serviceInfoPair.first;
        zkClient.create(servicePath);
        for(auto& methodPtrPair : serviceInfoPair.second.m_methodMap) {
            // /service_name/method_name
            std::string methodPath = servicePath + '/'+methodPtrPair.first;
            zkClient.create(methodPath);
            // 创建临时性节点 /service_name/method_name/ip:port
            std::string hostPath = methodPath+'/'+ip+':'+std::to_string(port);
            zkClient.create(hostPath, "", true);
        }
    }

    // 未使用unordered_map 缓存 method
    // for(auto& servicePtr : m_serviceMap) {
    //     //  /service_name
    //     std::string servicePath = '/'+servicePtr.first;
    //     zkClient.create(servicePath);
    //     auto serviceDescPtr = servicePtr.second->GetDescriptor();
    //     int methodCnt = serviceDescPtr->method_count();
    //     for(int i=0;i<methodCnt;i++) {
    //         // /service_name/method_name
    //         auto methodDescPtr = serviceDescPtr->method(i);
    //         std::string methodPath = servicePath+'/'+std::string(methodDescPtr->name());
    //         zkClient.create(methodPath);
    //         // 创建临时节点 /service_name/method_name/ip:port
    //         std::string hostPath = methodPath+'/'+ip+':'+std::to_string(port);
    //         zkClient.create(hostPath, "", true);
    //     }
    // }



    // 启动网络服务
    server.start();
    m_eventLoop.loop();

}

// 处理连接回调函数
void RpcProvider::onConnection(const muduo::net::TcpConnectionPtr& conn)
{
    if (!conn->connected()) {
        // 断开与rpc客户端连接
        conn->shutdown();
    }
}

// 处理读写事件回调函数
// 若远程有rpc调用请求, 会调用该回调函数
/*
在框架内部 RpcProvider(callee)和RpcConsumer(caller)协商好通信使用的protobuf数据类型
service_name method_name args
在proto定义message类型 进行数据头序列化和反序列化

header_size(4字节)
header: service_name method_name args_size
args

*/
void RpcProvider::onMessage(const muduo::net::TcpConnectionPtr& conn,
               muduo::net::Buffer* buffer,
               muduo::Timestamp)
{
    // 收到的rpc请求字符流 包含方法名和参数
    std::string recvBuf = buffer->retrieveAllAsString();

    // 读取header_size
    // 从字符流中读取前4个字节
    int32_t headerSize = 0;
    recvBuf.copy((char*)&headerSize, 4, 0);

    // 根据header_size 读取header
    std::string rpcHeaderStr = recvBuf.substr(4, headerSize);
    mprpc::RpcHeader rpcHeader;
    if (!rpcHeader.ParseFromString(rpcHeaderStr)) {
        // 数据头反序列化失败
        LOG("failed to parse from string to rpcHeader!");
        conn->shutdown();
        return;
    }
    std::string serviceName = rpcHeader.service_name();
    std::string methodName = rpcHeader.method_name();
    int32_t argsSize = rpcHeader.args_size();
    
    // 读取args
    std::string argsStr = recvBuf.substr(4+headerSize, argsSize);

    LOG("headerSize:" + std::to_string(headerSize));
    LOG("rpcHeaderStr:" + rpcHeaderStr);
    LOG("serviceName:" + serviceName);
    LOG("methodName:" + methodName);
    LOG("argsSize" + std::to_string(argsSize));
    LOG("argsStr:" + argsStr);
    
    // 获取service对象和method对象
    auto serviceInfoIt = m_serviceInfoMap.find(serviceName);
    if(serviceInfoIt==m_serviceInfoMap.end()) {
        LOG("failed to find service:" + serviceName + " in m_serviceInfoMap!");
        conn->shutdown();
        return;
    }

    auto& methodMap = serviceInfoIt->second.m_methodMap;
    auto methodIt = methodMap.find(methodName);
    if(methodIt == methodMap.end()) {
        LOG("failed to find method:" + methodName + " in methodMap!");
        conn->shutdown();
        return;
    }
    
    google::protobuf::Service* service = serviceInfoIt->second.m_service;
    const google::protobuf::MethodDescriptor* method = serviceInfoIt->second.m_methodMap[methodName];
    
    // 生成rpc远程过程调用的请求request和响应response
    google::protobuf::Message* request = service->GetRequestPrototype(method).New();
    if (!request->ParseFromString(argsStr)) {
        LOG("failed to parse from string to request! content:" + argsStr);
        conn->shutdown();
        return;
    }
    google::protobuf::Message* response = service->GetResponsePrototype(method).New();
    
    // 绑定Closure回调函数
    // template <typename Class, typename Arg1, typename Arg2>
    // inline Closure* NewCallback(Class* object, void (Class::*method)(Arg1, Arg2),
    //                         Arg1 arg1, Arg2 arg2)
    google::protobuf::Closure* done =
        google::protobuf::NewCallback<RpcProvider,
                                      const muduo::net::TcpConnectionPtr&,
                                      const google::protobuf::Message*>(this,
                                                                        &RpcProvider::sendRpcResponse,
                                                                        conn,
                                                                        response);

    // 执行相应的rpc方法
    service->CallMethod(method, nullptr, request, response, done);

}

// Closure回调函数 用于序列化rpc响应并发送回客户端
void RpcProvider::sendRpcResponse(const muduo::net::TcpConnectionPtr& conn, const google::protobuf::Message* response)
{
    // rpc响应序列化
    std::string responseStr;
    if (!response->SerializeToString(&responseStr)) {
        LOG("failed to serialize to string ! content:" + responseStr);
        conn->shutdown();
        return;
    }
    
    // 将响应发送到rpc调用端
    conn->send(responseStr);
    conn->shutdown(); // 短连接服务
}