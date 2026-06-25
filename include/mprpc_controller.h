#pragma once
#include <google/protobuf/service.h>
#include <string>

class MprpcController: public google::protobuf::RpcController
{
public:
    MprpcController();

    // Client-side methods ---------------------------------------------

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;

    // 未实现
    void StartCancel() override;

    // Server-side methods ---------------------------------------------

    void SetFailed(const std::string& reason) override;

    // 未实现功能
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

private:
    bool m_failed; // 执行状态
    std::string m_errorText; // rpc执行错误信息
};