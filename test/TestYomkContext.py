import YomkApi

# 初始化YomkApi
YomkApi.init(YomkApi.YomkServer(), ["YomkContext"])

# 创建上下文
res = YomkApi.context_create("ctx", "ctx_data")
print("context_create_ctx: key=ctx, value=ctx_data", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx: key=ctx, value=", ctx_data)

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set")
print("context_set_ctx: key=ctx, value=ctx_data_set", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx: key=ctx, value=", ctx_data)

# 开启上下文检查
res = YomkApi.context_turn_on_checker()
print("context_turn_on_checker", res.msg)

def context_checker_reject(ctx: YomkApi.Context)->YomkApi.CheckStatus:
    print("check reject func called: ctx=", ctx.key, "value=", ctx.value)
    return YomkApi.CheckStatus.eReject

# 设置上下文检查函数
res = YomkApi.context_set_checker("ctx", context_checker_reject)
print("context_set_checker_reject: key=ctx. checker=context_checker_reject", res.msg)

# 开启上下文监控
res = YomkApi.context_turn_on_monitor()
print("context_turn_on_monitor", res.msg)

def context_monitor_func(ctx: YomkApi.Context)->None:
    print("monitor func called: ctx=", ctx.key, "value=", ctx.value)   

# 设置上下文监控函数
res = YomkApi.context_set_monitor("ctx", context_monitor_func)
print("context_set_monitor: key=ctx, monitor=context_monitor_func", res.msg)  

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set_reject")
print("context_set_ctx: key=ctx, value=ctx_data_set_reject", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx: key=ctx, value=", ctx_data)

def context_checker_accept(ctx: YomkApi.Context)->YomkApi.CheckStatus:
    print("check accept func called: ctx=", ctx.key, "value=", ctx.value)
    return YomkApi.CheckStatus.eAccept

# 设置上下文检查函数
res = YomkApi.context_set_checker("ctx", context_checker_accept)
print("context_set_checker_accept: key=ctx. checker=context_checker_accept", res.msg)

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set_accept")
print("context_set_ctx: key=ctx, value=ctx_data_set_accept", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx: key=ctx, value=", ctx_data)

# 删除上下文
res = YomkApi.context_destroy("ctx")
print("context_destroy_ctx: key=ctx", res.msg)

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set")
print("context_set_ctx: key=ctx, value=ctx_data_set", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx: key=ctx, value=", ctx_data)
