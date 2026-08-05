#include "MathService.h"

MathService::MathService(YomkServer *server)
    : YomkService(server)
{
    name("/MathService");
}

int MathService::init()
{
    YomkInstallFunc("/add", MathService::add);
    YomkInstallFunc("/sub", MathService::sub);
    YomkInstallFunc("/mul", MathService::mul);
    YomkInstallFunc("/div", MathService::div);
    YOMK_INFO_TAG("MathService::init", "install func [ /add /sub /mul /div ] to", name());
    return 0;
}

YomkResponse MathService::add(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YMathOp, data);
    double result = data->req.a + data->req.b;
    YOMK_INFO_TAG("MathService::add", data->req.a, " + ", data->req.b, " = ", result);
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(Float64, result));
}

YomkResponse MathService::sub(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YMathOp, data);
    double result = data->req.a - data->req.b;
    YOMK_INFO_TAG("MathService::sub", data->req.a, " - ", data->req.b, " = ", result);
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(Float64, result));
}

YomkResponse MathService::mul(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YMathOp, data);
    double result = data->req.a * data->req.b;
    YOMK_INFO_TAG("MathService::mul", data->req.a, " * ", data->req.b, " = ", result);
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(Float64, result));
}

YomkResponse MathService::div(YomkPkgPtr pkg)
{
    YomkUnPackPkgResponse(pkg, YMathOp, data);
    if (data->req.b == 0.0)
    {
        YOMK_ERROR_TAG("MathService::div", "division by zero");
        return YomkResponse(YomkResponse::eNo, "division by zero");
    }
    double result = data->req.a / data->req.b;
    YOMK_INFO_TAG("MathService::div", data->req.a, " / ", data->req.b, " = ", result);
    return YomkResponse(YomkResponse::eOk, "ok", YomkMkPtr(Float64, result));
}
