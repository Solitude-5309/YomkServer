#pragma once
#include <YomkServer/YomkAPI.h>

using namespace yomk;

// 请求消息包：运算符 + 两个操作数
struct MathOp { std::string op; double a; double b; };
YomkMsg(MathOp, YMathOp, req)

class MathService : public YomkService
{
public:
    MathService(YomkServer *server);
    virtual ~MathService() {}
    virtual int init() override;

private:
    YomkResponse add(YomkPkgPtr pkg);
    YomkResponse sub(YomkPkgPtr pkg);
    YomkResponse mul(YomkPkgPtr pkg);
    YomkResponse div(YomkPkgPtr pkg);
};
