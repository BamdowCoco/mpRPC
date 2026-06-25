#pragma once

#include "mprpc_config.h"
#include "mprpc_channel.h"
#include "mprpc_controller.h"

// mprpc框架基础类 用于框架初始化
class MprpcApplication
{
public:
    static void init(int argc, char** argv);

    static MprpcApplication& getInstance();
    static const MprpcConfig& getConfig();

private:
    static MprpcConfig m_config;
    static bool m_hasConfigured;

    MprpcApplication() {}
    MprpcApplication(const MprpcApplication&) = delete;
    MprpcApplication(MprpcApplication&&) = delete;
};