#pragma once
#include "YomkExport.h"

// 启动编排接口：按 before -> start -> after 顺序执行。
// 独立成头：YomkBoot 是纯虚编排契约，与 YomkApi 的请求/日志封装逻辑无耦合；
// 自定义 Boot 类包含本头即可，无需拉入整个 YomkApi.h
class YOMKSERVER_EXPORT YomkBoot
{
public:
    virtual ~YomkBoot() {}
    virtual int before() = 0;
    virtual int start() = 0;
    virtual int after() = 0;
};
