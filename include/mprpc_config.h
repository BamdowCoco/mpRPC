#pragma once

#include <unordered_map>
#include <string>

// 框架读取配置文件类
// rpc_server_ip rpc_server_port zookeeper_ip zookeeper_port
class MprpcConfig
{
public:
    // 加载解析配置文件
    bool loadConfigFile(std::string configFile);
    // 查找对应key的配置信息
    std::string load(const std::string& key) const;

private:
    std::unordered_map<std::string, std::string> m_configMap;

    // 去除空格字符
    void removeSpace(std::string& str);
};