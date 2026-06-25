#include <cstdio>
#include <cstdlib>
#include <google/protobuf/map.h>
#include <iostream>
#include <ostream>
#include <string>

#include "test.pb.h"
using namespace fixbug;

void testRequest() {
    // 封装 登录请求对象
    LoginRequest req;
    req.set_name("zhang san");
    req.set_pwd("123456");

    // 对象序列化 => char*
    std::string sendStr;
    if(req.SerializeToString(&sendStr)) {
        std::cout << sendStr.c_str() << std::endl;
    } else {
        std::cerr << "failed to serialize" << std::endl;
        exit(1);
    }

    // char* 反序列化 => 登录请求对象
    LoginRequest reqB;
    if(reqB.ParseFromString(sendStr)) {
        std::cout << reqB.name() << std::endl;
        std::cout << reqB.pwd() << std::endl;
    } else {
        std::cerr << "failed to parse from string" << std::endl;
        exit(1);
    }
}

void testResponse() {
    LoginResponse rsp;
    // mutable_result 返回rsp对象中 result变量的指针
    ResultCode* rc = rsp.mutable_result();
    rc->set_errcode(0);
    rc->set_errmsg("");
    rsp.set_success(true);

    // 序列化
    std::string sendStr;
    if (rsp.SerializeToString(&sendStr)) {
        for(unsigned char c : sendStr) {
            printf("%02X ", c);
        }
        std::cout << std::endl;
    } else {
        std::cerr << "failed to serialize to string!" << std::endl;
    }

    // 反序列化
    LoginResponse rspB;
    if (rspB.ParseFromString(sendStr)) {
        const ResultCode& rcB = rspB.result();
        std::cout << rcB.errcode() << std::endl;
        std::cout << rspB.success() << std::endl;
    } else {
        std::cerr << "failed to parse from string!" << std::endl;
    }

}

void testGetFriendListRsp() {
    GetFriendListsResponse rsp;
    ResultCode* rc = rsp.mutable_result();
    rc->set_errcode(0);

    User* user1 = rsp.add_friend_list();
    user1->set_name("zhang san");
    user1->set_age(20);
    user1->set_sex(fixbug::User_Sex_MALE);

    User* user2 = rsp.add_friend_list();
    user2->set_name("li si");
    user2->set_age(23);
    user2->set_sex(fixbug::User_Sex_MALE);

    User* user3 = rsp.add_friend_list();
    user3->set_name("xiao mei");
    user3->set_age(20);
    user3->set_sex(fixbug::User_Sex_FEMALE);

    std::cout << rsp.friend_list_size() << std::endl;

    for(int i=0;i<rsp.friend_list_size();i++) {
        User user = rsp.friend_list(i);
        std::cout << i << ':';
        std::cout << user.name() << std::endl;

    }
}

void testProtoMap() {
    TestMap testMap;
    // google::protobuf::Map<int32_t, std::string>* mp = testMap.mutable_test();
    auto mp = testMap.mutable_test();
    mp->insert({1,"1234"});
    std::cout << mp->at(1) << std::endl;

}

void testService() {
    class UserService : public fixbug::UserServiceRpc
    {
    public:
        bool Login(const std::string name, const std::string pwd)
        {
            std::cout << "doing local service: Login" << std::endl;
            std::cout << "name:" << name << " pwd:" << pwd << std::endl;
            return true;
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
    };

    google::protobuf::Service* service = new UserService;
    const google::protobuf::MethodDescriptor* method1 = service->GetDescriptor()->method(0);
    if(method1 == nullptr) {
        std::cout << "method1 is unknown method!" << std::endl;
    }
    // const google::protobuf::MethodDescriptor* method2 = service->GetDescriptor()->FindMethodByName(method1->name());
    // if (method1 == method2) {
    //     std::cout << "method1 == method2!!!" << std::endl;
    // } else {
    //     std::cout << "method1 != method2" << std::endl;
    // }

    const google::protobuf::MethodDescriptor* unknownMethod = service->GetDescriptor()->FindMethodByName("testUnknownMethod");
    if(unknownMethod == nullptr) {
        std::cout << "this is unknown method!" << std::endl;
    }

    
}

int main() {
    // testRequest();
    // testResponse();
    // testGetFriendListRsp();
    // testProtoMap();
    testService();
    return 0;
}