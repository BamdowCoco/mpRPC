#include <zookeeper/zookeeper.h>
#include <string>
#include <vector>

class ZKClient
{
public:
    ZKClient();
    ~ZKClient();

    // 启动zkclient 连接zkserver
    void start();
    // 在zkserver上指定路径创建znode节点
    void create(const std::string path, const std::string data="", bool isEphemeral = false);
    // 获取指定节点路径的值
    std::string getData(const std::string path);
    // 获取指定节点路径 的 所有孩子名字
    std::vector<std::string> getChildren(const std::string path);
private:
    // zk客户端句柄
    zhandle_t* m_zhandle;
};