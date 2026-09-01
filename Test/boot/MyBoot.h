#pragma once
#include "YomkAPI.h"

/**
 * @brief 程序生命周期管理类
 * 
 * 继承 YomkBoot，实现三阶段初始化流程：
 * 1. before(): 服务启动前的资源准备
 * 2. start(): 注册并启动服务
 * 3. after(): 服务启动后的善后操作
 * 
 * 使用方式：
 * YOMK_BOOT(new MyBoot({"/YomkServiceA", "/YomkServiceB"}));
 */
class MyBoot : public YomkBoot
{
public:
    /**
     * @brief 构造函数
     * @param startSrvNames 需要启动的服务名称列表
     * 
     * 示例：MyBoot({"/YomkServiceA", "/YomkServiceB"})
     */
    MyBoot(const std::vector<std::string> &startSrvNames = {}) : m_startSrvNames(startSrvNames) {}
    
    /// 服务启动前：创建 Context、EventLoop、注册 FunctionPool 等
    int before();
    
    /// 注册并启动服务
    int start();
    
    /// 服务启动后：调用服务接口做初始化
    int after();

private:
    std::vector<std::string> m_startSrvNames; // 将要启动的服务清单，实际业务按需启动
};
