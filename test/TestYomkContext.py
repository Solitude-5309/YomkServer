import YomkApi

# 初始化YomkApi
YomkApi.init(YomkApi.YomkServer(), ["YomkContext"])

# 创建上下文
res = YomkApi.context_create("ctx", "ctx_data")
print("context_create_ctx", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx", ctx_data)

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set")
print("context_set_ctx", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx", ctx_data)

# 删除上下文
res = YomkApi.context_destroy("ctx")
print("context_destroy_ctx", res.msg)

# 设置上下文数据
res = YomkApi.context_set("ctx", "ctx_data_set")
print("context_set_ctx", res.msg)

# 获取上下文数据
ctx_data = YomkApi.context_get("ctx", "ctx_data_default")
print("context_get_ctx", ctx_data)