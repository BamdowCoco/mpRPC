#include "mprpc_controller.h"

MprpcController::MprpcController()
{
    m_failed = false;
    m_errorText = "";
}

// Client-side methods ---------------------------------------------

void MprpcController::Reset()
{
    m_failed = false;
    m_errorText = "";
}

bool MprpcController::Failed() const
{
    return m_failed;
}

std::string MprpcController::ErrorText() const
{
    return m_errorText;
}

// 未实现
void MprpcController::StartCancel() {}

// Server-side methods ---------------------------------------------

void MprpcController::SetFailed(const std::string& reason)
{
    m_failed = true;
    m_errorText = reason;
}

// 未实现功能
bool MprpcController::IsCanceled() const { return false; }
void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback) {}