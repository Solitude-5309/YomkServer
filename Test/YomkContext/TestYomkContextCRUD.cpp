/**
 * @file TestYomkContextCRUD.cpp
 * @brief YomkContext CRUD 与边界值白盒测试（Context 模块 MC1）
 *
 * 覆盖内容：
 * 1. create：正常创建、空 key、空 value、重复 key
 * 2. destroy：正常销毁、不存在 key、空 key、重复销毁
 * 3. get：正常 get、不存在 key（返回默认 value）、空 key（返回默认 value）
 * 4. set：正常 set、不存在 key、空 key、类型不匹配
 * 5. keys/listAll/keyInfo：随增删变化、空 key、不存在 key、信息行格式
 *
 * 说明：全程使用 YOMK_INIT 单例拉起内置 /YomkContext，测试通过 YOMK_CONTEXT_* API 宏调用；
 *       不触发 checker/monitor，不触发 shutdown 排空（归 MC2/MC3）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "YomkAPI.h"

static int g_failed = 0;

#define CHECK(cond, msg)                                                          \
    do                                                                            \
    {                                                                             \
        if (!(cond))                                                              \
        {                                                                         \
            std::cout << "[FAIL] [line " << __LINE__ << "] " << msg << std::endl; \
            ++g_failed;                                                           \
        }                                                                         \
        else                                                                      \
        {                                                                         \
            std::cout << "[ OK ] [line " << __LINE__ << "] " << msg << std::endl; \
        }                                                                         \
    } while (0)

static bool contains(const std::vector<std::string> &vec, const std::string &item)
{
    for (auto &v : vec)
    {
        if (v == item)
        {
            return true;
        }
    }
    return false;
}

int main()
{
    // 拉起内置服务 /YomkContext
    auto server = YOMK_INIT(1);
    CHECK(server != nullptr, "YOMK_INIT 返回非空服务器");

    // 验证 /YomkContext 已注册
    auto infoAll = YOMK_SERVER_INFO_ALL();
    CHECK(infoAll.m_status == YomkResponse::eOk, "SERVER_INFO_ALL 返回 eOk");

    // create：正常创建 String 类型 value
    {
        auto resp = YOMK_CONTEXT_CREATE("str_key", YomkMkPtr(String, std::string("hello")));
        CHECK(resp.m_status == YomkResponse::eOk, "create 正常 String value 返回 eOk");
        CHECK(resp.m_msg == "create context success", "create 成功消息一致");
    }

    // create：正常创建 Int32 类型 value
    {
        auto resp = YOMK_CONTEXT_CREATE("int_key", YomkMkPtr(Int32, 42));
        CHECK(resp.m_status == YomkResponse::eOk, "create 正常 Int32 value 返回 eOk");
    }

    // create：空 key
    {
        auto resp = YOMK_CONTEXT_CREATE("", YomkMkPtr(String, std::string("value")));
        CHECK(resp.m_status == YomkResponse::eNo, "create 空 key 返回 eNo");
        CHECK(resp.m_msg == "key is empty", "create 空 key 错误消息一致");
    }

    // create：空 value（nullptr）
    {
        auto resp = YOMK_CONTEXT_CREATE("null_value_key", nullptr);
        CHECK(resp.m_status == YomkResponse::eNo, "create nullptr value 返回 eNo");
        CHECK(resp.m_msg == "value is empty", "create nullptr value 错误消息一致");
    }

    // create：重复 key
    {
        auto resp = YOMK_CONTEXT_CREATE("str_key", YomkMkPtr(String, std::string("dup")));
        CHECK(resp.m_status == YomkResponse::eNo, "create 重复 key 返回 eNo");
        CHECK(resp.m_msg == "key already exists", "create 重复 key 错误消息一致");
    }

    // get：正常 get
    {
        auto defaultValue = YomkMkPtr(String, std::string("default"));
        auto got = YOMK_CONTEXT_GET(String, "str_key", defaultValue);
        CHECK(got != nullptr, "get 正常返回非空");
        CHECK(got && got->d == "hello", "get 正常返回原始 value");
    }

    // get：不存在 key，返回默认 value
    {
        auto defaultValue = YomkMkPtr(String, std::string("default"));
        auto got = YOMK_CONTEXT_GET(String, "no_such_key", defaultValue);
        CHECK(got != nullptr, "get 不存在 key 返回默认值指针非空");
        CHECK(got && got->d == "default", "get 不存在 key 返回默认值");
    }

    // get：空 key，返回默认 value
    {
        auto defaultValue = YomkMkPtr(String, std::string("default"));
        auto got = YOMK_CONTEXT_GET(String, "", defaultValue);
        CHECK(got != nullptr, "get 空 key 返回默认值指针非空");
        CHECK(got && got->d == "default", "get 空 key 返回默认值");
    }

    // set：正常 set 同类型 value
    {
        auto resp = YOMK_CONTEXT_SET("str_key", YomkMkPtr(String, std::string("world")));
        CHECK(resp.m_status == YomkResponse::eOk, "set 正常同类型返回 eOk");
        CHECK(resp.m_msg == "set context success", "set 成功消息一致");

        auto defaultValue = YomkMkPtr(String, std::string("default"));
        auto got = YOMK_CONTEXT_GET(String, "str_key", defaultValue);
        CHECK(got && got->d == "world", "set 后 get 返回新 value");
    }

    // set：不存在 key
    {
        auto resp = YOMK_CONTEXT_SET("no_such_key", YomkMkPtr(String, std::string("x")));
        CHECK(resp.m_status == YomkResponse::eNo, "set 不存在 key 返回 eNo");
        CHECK(resp.m_msg == "key is not exist", "set 不存在 key 错误消息一致");
    }

    // set：空 key
    {
        auto resp = YOMK_CONTEXT_SET("", YomkMkPtr(String, std::string("x")));
        CHECK(resp.m_status == YomkResponse::eNo, "set 空 key 返回 eNo");
        CHECK(resp.m_msg == "key is empty", "set 空 key 错误消息一致");
    }

    // set：类型不匹配
    {
        auto resp = YOMK_CONTEXT_SET("str_key", YomkMkPtr(Int32, 100));
        CHECK(resp.m_status == YomkResponse::eNo, "set 类型不匹配返回 eNo");
        CHECK(resp.m_msg == "context type not match", "set 类型不匹配错误消息一致");
    }

    // keys：包含已创建 key
    {
        auto resp = YOMK_CONTEXT_INFO_KEYS();
        CHECK(resp.m_status == YomkResponse::eOk, "keys 返回 eOk");
        YomkUnPackPkg(resp.m_data, StringArray, arr);
        CHECK(arr != nullptr, "keys 数据为 StringArray");
        CHECK(arr && arr->d.size() == 2, "keys 返回 2 个 key");
        CHECK(arr && contains(arr->d, "str_key"), "keys 包含 str_key");
        CHECK(arr && contains(arr->d, "int_key"), "keys 包含 int_key");
    }

    // keyInfo：正常 key
    {
        auto resp = YOMK_CONTEXT_INFO_KEY("str_key");
        CHECK(resp.m_status == YomkResponse::eOk, "keyInfo 正常 key 返回 eOk");
        CHECK(resp.m_msg.find("str_key") != std::string::npos, "keyInfo 消息包含 key");
        CHECK(resp.m_msg.find("String") != std::string::npos, "keyInfo 消息包含类型名 String");
        CHECK(resp.m_msg.find("checker:off") != std::string::npos, "keyInfo 消息包含 checker:off");
        CHECK(resp.m_msg.find("monitors:0") != std::string::npos, "keyInfo 消息包含 monitors:0");
    }

    // keyInfo：空 key
    {
        auto resp = YOMK_CONTEXT_INFO_KEY("");
        CHECK(resp.m_status == YomkResponse::eNo, "keyInfo 空 key 返回 eNo");
        CHECK(resp.m_msg == "key is empty", "keyInfo 空 key 错误消息一致");
    }

    // keyInfo：不存在 key
    {
        auto resp = YOMK_CONTEXT_INFO_KEY("no_such_key");
        CHECK(resp.m_status == YomkResponse::eNo, "keyInfo 不存在 key 返回 eNo");
        CHECK(resp.m_msg == "key is not exist", "keyInfo 不存在 key 错误消息一致");
    }

    // listAll：包含所有 key 信息
    {
        auto resp = YOMK_CONTEXT_INFO_ALL();
        CHECK(resp.m_status == YomkResponse::eOk, "listAll 返回 eOk");
        YomkUnPackPkg(resp.m_data, StringArray, arr);
        CHECK(arr != nullptr, "listAll 数据为 StringArray");
        CHECK(arr && arr->d.size() == 2, "listAll 返回 2 行信息");
    }

    // destroy：正常销毁
    {
        auto resp = YOMK_CONTEXT_DESTROY("int_key");
        CHECK(resp.m_status == YomkResponse::eOk, "destroy 正常销毁返回 eOk");
        CHECK(resp.m_msg == "destroy context success", "destroy 成功消息一致");
    }

    // destroy：重复销毁
    {
        auto resp = YOMK_CONTEXT_DESTROY("int_key");
        CHECK(resp.m_status == YomkResponse::eNo, "destroy 重复销毁返回 eNo");
        CHECK(resp.m_msg == "key is not exist", "destroy 重复销毁错误消息一致");
    }

    // destroy：不存在 key
    {
        auto resp = YOMK_CONTEXT_DESTROY("no_such_key");
        CHECK(resp.m_status == YomkResponse::eNo, "destroy 不存在 key 返回 eNo");
        CHECK(resp.m_msg == "key is not exist", "destroy 不存在 key 错误消息一致");
    }

    // destroy：空 key（源码不单独判空，等价于不存在的 key）
    {
        auto resp = YOMK_CONTEXT_DESTROY("");
        CHECK(resp.m_status == YomkResponse::eNo, "destroy 空 key 返回 eNo");
        CHECK(resp.m_msg == "key is not exist", "destroy 空 key 错误消息一致");
    }

    // keys：销毁后只剩一个 key
    {
        auto resp = YOMK_CONTEXT_INFO_KEYS();
        CHECK(resp.m_status == YomkResponse::eOk, "keys 销毁后返回 eOk");
        YomkUnPackPkg(resp.m_data, StringArray, arr);
        CHECK(arr && arr->d.size() == 1, "keys 销毁后返回 1 个 key");
        CHECK(arr && contains(arr->d, "str_key"), "keys 销毁后仍包含 str_key");
        CHECK(arr && !contains(arr->d, "int_key"), "keys 销毁后不含 int_key");
    }

    // 清理：销毁剩余 key
    {
        auto resp = YOMK_CONTEXT_DESTROY("str_key");
        CHECK(resp.m_status == YomkResponse::eOk, "清理销毁 str_key 返回 eOk");
    }

    // keys：全部销毁后为空
    {
        auto resp = YOMK_CONTEXT_INFO_KEYS();
        CHECK(resp.m_status == YomkResponse::eOk, "keys 全部销毁后返回 eOk");
        YomkUnPackPkg(resp.m_data, StringArray, arr);
        CHECK(arr && arr->d.empty(), "keys 全部销毁后返回空数组");
    }

    YOMK_SHUTDOWN();

    if (g_failed == 0)
    {
        std::cout << "TestYomkContextCRUD all check passed." << std::endl;
        return 0;
    }
    else
    {
        std::cout << "TestYomkContextCRUD FAILED (" << g_failed << " checks failed)." << std::endl;
        return 1;
    }
}
