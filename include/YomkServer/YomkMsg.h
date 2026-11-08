#pragma once
#include <memory>
#include <string>

/**
 * @brief 消息类型基类：运行时类型名载体（kName 比对对象）
 * @note 自定义消息经 YOMK_DEFINE_MSG 宏派生, 不直接继承本类手工命名
 */
class YomkMsg
{
public:
    YomkMsg() {}
    virtual ~YomkMsg() {}

public:
    void name(const std::string& name) { m_name = name; }
    std::string name() { return m_name; }

protected:
    std::string m_name;
};
using YomkMsgPtr = std::shared_ptr<YomkMsg>;