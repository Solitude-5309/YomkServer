/**
 * @file TestYomkContextCRUD.cpp
 * @brief YomkContext CRUD 与边界值白盒测试（Context 模块 MC1）
 *
 * 覆盖内容：
 * 1. create：正常创建、空 key、空 value、重复 key
 * 2. destroy：正常销毁、不存在 key、空 key、重复销毁
 * 3. get：正常 get、不存在 key（返回默认 value）、空 key（返回默认 value）、
 *    类型不匹配（返回默认 value，P3 增补——ResStatus::Succeeded 路径运行时名解包失败）
 * 4. set：正常 set、不存在 key、空 key、类型不匹配
 * 5. keys/listAll/keyInfo：随增删变化、空 key、不存在 key、字段断言
 *
 * 说明：全程使用 yomk::server().init() 单例拉起内置 /YomkContext，测试通过 yomk::ctx() 域链式 op 调用；
 *       不触发 checker/monitor，不触发 shutdown 排空（归 MC2/MC3）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "YomkApi.h"

static int g_failed = 0;

#define YOMK_TEST_CHECK(cond, msg)                                                          \
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

static bool contains(const std::vector<std::string>& vec, const std::string& item)
{
    for (auto& v : vec)
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
    yomk::server().async_thread(1).init();
    auto server = yomk::server().instance();
    YOMK_TEST_CHECK(server != nullptr, "yomk::server().init() 返回非空服务器");

    // 验证 /YomkContext 已注册
    auto infoAll = yomk::server().all();
    YOMK_TEST_CHECK(infoAll.m_status == yomk::payload::ResStatus::Succeeded, "all() 返回 ResStatus::Succeeded");

    // create：正常创建 String 类型 value
    {
        auto resp = yomk::ctx().key("str_key").value(yomk::msgs::String::shared(std::string("hello"))).create();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "create 正常 String value 返回 ResStatus::Succeeded");
    }

    // create：正常创建 Int32 类型 value
    {
        auto resp = yomk::ctx().key("int_key").value(yomk::msgs::Int32::shared(42)).create();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "create 正常 Int32 value 返回 ResStatus::Succeeded");
    }

    // create：空 key
    {
        auto resp = yomk::ctx().key("").value(yomk::msgs::String::shared(std::string("value"))).create();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "create 空 key 返回 ResStatus::Failed");
    }

    // create：空 value（nullptr）
    {
        auto resp = yomk::ctx().key("null_value_key").create();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "create nullptr value 返回 ResStatus::Failed");
    }

    // create：重复 key
    {
        auto resp = yomk::ctx().key("str_key").value(yomk::msgs::String::shared(std::string("dup"))).create();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "create 重复 key 返回 ResStatus::Failed");
    }

    // get：正常 get
    {
        auto defaultValue = yomk::msgs::String::shared(std::string("default"));
        auto got = yomk::ctx().key("str_key").msg<yomk::msgs::String>().with_default(defaultValue).get();
        YOMK_TEST_CHECK(got != nullptr, "get 正常返回非空");
        YOMK_TEST_CHECK(got && got->d == "hello", "get 正常返回原始 value");
    }

    // get：不存在 key，返回默认 value
    {
        auto defaultValue = yomk::msgs::String::shared(std::string("default"));
        auto got = yomk::ctx().key("no_such_key").msg<yomk::msgs::String>().with_default(defaultValue).get();
        YOMK_TEST_CHECK(got != nullptr, "get 不存在 key 返回默认值指针非空");
        YOMK_TEST_CHECK(got && got->d == "default", "get 不存在 key 返回默认值");
    }

    // get：空 key，返回默认 value
    {
        auto defaultValue = yomk::msgs::String::shared(std::string("default"));
        auto got = yomk::ctx().key("").msg<yomk::msgs::String>().with_default(defaultValue).get();
        YOMK_TEST_CHECK(got != nullptr, "get 空 key 返回默认值指针非空");
        YOMK_TEST_CHECK(got && got->d == "default", "get 空 key 返回默认值");
    }

    // get：类型不匹配，返回调用方默认 value（ResStatus::Succeeded 路径运行时名解包失败，P3 增补）
    {
        auto cr = yomk::ctx().key("mismatch_key").value(yomk::msgs::String::shared(std::string("strval"))).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mismatch_key（String 值）成功");
        auto intDefault = yomk::msgs::Int32::shared(777);
        auto got = yomk::ctx().key("mismatch_key").msg<yomk::msgs::Int32>().with_default(intDefault).get();
        YOMK_TEST_CHECK(got != nullptr && got == intDefault, "get 类型不匹配返回调用方默认值（指针一致）");
        YOMK_TEST_CHECK(got && got->d == 777, "get 类型不匹配默认值内容保持 777");
        YOMK_TEST_CHECK(yomk::ctx().key("mismatch_key").destroy() == yomk::payload::ResStatus::Succeeded, "销毁 mismatch_key（防污染后续 keys 计数）");
    }

    // get：类型不匹配第二变体（Bool 存 → String GET，验证 UnPackPkgT 通用失败路径）
    {
        auto cr = yomk::ctx().key("mismatch_key2").value(yomk::msgs::Bool::shared(true)).create();
        YOMK_TEST_CHECK(cr == yomk::payload::ResStatus::Succeeded, "创建 mismatch_key2（Bool 值）成功");
        auto strDefault = yomk::msgs::String::shared(std::string("fallback"));
        auto got = yomk::ctx().key("mismatch_key2").msg<yomk::msgs::String>().with_default(strDefault).get();
        YOMK_TEST_CHECK(got != nullptr && got == strDefault, "get 第二变体类型不匹配返回默认值（指针一致）");
        YOMK_TEST_CHECK(got && got->d == "fallback", "get 第二变体默认值内容保持 fallback");
        YOMK_TEST_CHECK(yomk::ctx().key("mismatch_key2").destroy() == yomk::payload::ResStatus::Succeeded, "销毁 mismatch_key2（防污染后续 keys 计数）");
    }

    // set：正常 set 同类型 value
    {
        auto resp = yomk::ctx().key("str_key").value(yomk::msgs::String::shared(std::string("world"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "set 正常同类型返回 ResStatus::Succeeded");

        auto defaultValue = yomk::msgs::String::shared(std::string("default"));
        auto got = yomk::ctx().key("str_key").msg<yomk::msgs::String>().with_default(defaultValue).get();
        YOMK_TEST_CHECK(got && got->d == "world", "set 后 get 返回新 value");
    }

    // set：不存在 key
    {
        auto resp = yomk::ctx().key("no_such_key").value(yomk::msgs::String::shared(std::string("x"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "set 不存在 key 返回 ResStatus::Failed");
    }

    // set：空 key
    {
        auto resp = yomk::ctx().key("").value(yomk::msgs::String::shared(std::string("x"))).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "set 空 key 返回 ResStatus::Failed");
    }

    // set：类型不匹配
    {
        auto resp = yomk::ctx().key("str_key").value(yomk::msgs::Int32::shared(100)).set();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "set 类型不匹配返回 ResStatus::Failed");
    }

    // keys：包含已创建 key
    {
        auto resp = yomk::ctx().keys();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "keys 返回 ResStatus::Succeeded");
        auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.m_data);
        YOMK_TEST_CHECK(arr != nullptr, "keys 数据为 StringArray");
        YOMK_TEST_CHECK(arr && arr->d.size() == 2, "keys 返回 2 个 key");
        YOMK_TEST_CHECK(arr && contains(arr->d, "str_key"), "keys 包含 str_key");
        YOMK_TEST_CHECK(arr && contains(arr->d, "int_key"), "keys 包含 int_key");
    }

    // keyInfo：正常 key（结构化返回，m_msg 恒 "ok"，数据走 m_data）
    {
        auto resp = yomk::ctx().key("str_key").info();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "keyInfo 正常 key 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(resp.m_msg == "ok", "keyInfo 命中 m_msg 恒为 ok");
        auto info = yomk::msgs::unpack<yomk::msgs::ContextKeyInfo>(resp.m_data);
        YOMK_TEST_CHECK(info != nullptr, "keyInfo 数据为 ContextKeyInfo 包");
        YOMK_TEST_CHECK(info && info->d.m_key == "str_key", "keyInfo 字段 m_key 一致");
        YOMK_TEST_CHECK(info && info->d.m_typeName == "String", "keyInfo 字段 m_typeName 一致");
        YOMK_TEST_CHECK(info && !info->d.m_checkerOn, "keyInfo 字段 m_checkerOn 为 false");
        YOMK_TEST_CHECK(info && info->d.m_monitorCount == 0 && info->d.m_asyncMonitorCount == 0,
              "keyInfo 字段 monitor 计数为 0");
        YOMK_TEST_CHECK(info && info->d.m_group == "main", "keyInfo 字段 m_group 一致");
    }

    // keyInfo：空 key
    {
        auto resp = yomk::ctx().key("").info();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Failed, "keyInfo 空 key 返回 ResStatus::Failed");
        YOMK_TEST_CHECK(resp.m_msg == "key is empty", "keyInfo 空 key 错误消息一致");
    }

    // keyInfo：不存在 key
    {
        auto resp = yomk::ctx().key("no_such_key").info();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Failed, "keyInfo 不存在 key 返回 ResStatus::Failed");
        YOMK_TEST_CHECK(resp.m_msg == "key is not exist", "keyInfo 不存在 key 错误消息一致");
    }

    // listAll：跨组聚合（组 → 键元信息两层结构化包，m_msg 恒 "ok"）
    {
        auto resp = yomk::ctx().all();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "listAll 返回 ResStatus::Succeeded");
        YOMK_TEST_CHECK(resp.m_msg == "ok", "listAll 成功消息恒为 ok");
        auto arr = yomk::msgs::unpack<yomk::msgs::ContextGroupInfoList>(resp.m_data);
        YOMK_TEST_CHECK(arr != nullptr, "listAll 数据为 ContextGroupInfoList");
        YOMK_TEST_CHECK(arr && arr->d.size() == 1, "listAll 返回 1 个组信息");
        YOMK_TEST_CHECK(arr && arr->d[0].m_groupName == "main", "listAll 组名为 main");
        // 组内 m_keys 为 std::map 快照，中序遍历字典序：int_key < str_key；逐字段断言（均未挂 checker/monitor）
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys.size() == 2, "listAll 返回 2 个键信息");
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys[0].m_key == "int_key" && arr->d[0].m_keys[1].m_key == "str_key",
              "listAll 键序为 map 字典序（int_key, str_key）");
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys[0].m_typeName == "Int32" && arr->d[0].m_keys[1].m_typeName == "String",
              "listAll 类型名一致");
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys[0].m_group == "main" && arr->d[0].m_keys[1].m_group == "main",
              "listAll 键所属组一致");
        YOMK_TEST_CHECK(arr && !arr->d[0].m_keys[0].m_checkerOn && !arr->d[0].m_keys[1].m_checkerOn,
              "listAll checker 状态为关");
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys[0].m_monitorCount == 0 && arr->d[0].m_keys[1].m_monitorCount == 0,
              "listAll monitor 计数为 0");
        YOMK_TEST_CHECK(arr && arr->d[0].m_keys[0].m_asyncMonitorCount == 0 && arr->d[0].m_keys[1].m_asyncMonitorCount == 0,
              "listAll async monitor 计数为 0");
    }

    // destroy：正常销毁
    {
        auto resp = yomk::ctx().key("int_key").destroy();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "destroy 正常销毁返回 ResStatus::Succeeded");
    }

    // destroy：重复销毁
    {
        auto resp = yomk::ctx().key("int_key").destroy();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "destroy 重复销毁返回 ResStatus::Failed");
    }

    // destroy：不存在 key
    {
        auto resp = yomk::ctx().key("no_such_key").destroy();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "destroy 不存在 key 返回 ResStatus::Failed");
    }

    // destroy：空 key（源码不单独判空，等价于不存在的 key）
    {
        auto resp = yomk::ctx().key("").destroy();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Failed, "destroy 空 key 返回 ResStatus::Failed");
    }

    // keys：销毁后只剩一个 key
    {
        auto resp = yomk::ctx().keys();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "keys 销毁后返回 ResStatus::Succeeded");
        auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.m_data);
        YOMK_TEST_CHECK(arr && arr->d.size() == 1, "keys 销毁后返回 1 个 key");
        YOMK_TEST_CHECK(arr && contains(arr->d, "str_key"), "keys 销毁后仍包含 str_key");
        YOMK_TEST_CHECK(arr && !contains(arr->d, "int_key"), "keys 销毁后不含 int_key");
    }

    // 清理：销毁剩余 key
    {
        auto resp = yomk::ctx().key("str_key").destroy();
        YOMK_TEST_CHECK(resp == yomk::payload::ResStatus::Succeeded, "清理销毁 str_key 返回 ResStatus::Succeeded");
    }

    // keys：全部销毁后为空
    {
        auto resp = yomk::ctx().keys();
        YOMK_TEST_CHECK(resp.m_status == yomk::payload::ResStatus::Succeeded, "keys 全部销毁后返回 ResStatus::Succeeded");
        auto arr = yomk::msgs::unpack<yomk::msgs::StringArray>(resp.m_data);
        YOMK_TEST_CHECK(arr && arr->d.empty(), "keys 全部销毁后返回空数组");
    }

    yomk::server().shutdown();

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
