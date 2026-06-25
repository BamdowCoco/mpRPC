#include "mprpc_config.h"
#include "logger.h"

#include <fstream>
#include <cctype>

// 加载解析配置文件
bool MprpcConfig::loadConfigFile(std::string configFile)
{
    // 打开文件
    std::ifstream inFile(configFile);
    if(!inFile)
    {
        LOG("failed to open file:" + configFile);
        return false;
    }

    // 逐行读取文件
    std::string line;
    while (std::getline(inFile, line)) {
        // 跳过空行和注释
        if(line.empty() || line[0]=='#' || line[0]=='[') {
            continue;
        }
        // 去除空白字符
        removeSpace(line);
        // 读取key value
        int idx = line.find('=');
        std::string key = line.substr(0, idx);
        std::string value = line.substr(idx+1);
        // 加入 configmap
        m_configMap.insert({key, value});
    }
    // 关闭文件
    inFile.close();
    return true;
}

// 查找对应key的配置信息
std::string MprpcConfig::load(const std::string& key) const
{
    auto it = m_configMap.find(key);
    if(it == m_configMap.end()) {
        return {};
    }
    return it->second;
}

// 去除空格字符
void MprpcConfig::removeSpace(std::string& str)
{
    int index = 0;
    for(int i=0;i<str.size();i++) {
        if(!std::isspace(static_cast<unsigned char>(str[i]))) {
            str[index++] = str[i];
        }
    }
    str.resize(index);
}