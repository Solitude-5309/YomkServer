/**
 * @file TestYomkPkg.cpp
 * @brief YomkPkg 消息包体系独立测试（正式全面测试，P2——标准类型全集/解包语义全分支此前散点触达，无独立覆盖）
 *
 * 覆盖内容（5 个 Section，约 66 断言）：
 * S1. 标准类型消息包全集（15 标量 + 15 Array = 30 类型）：逐类型构造 name() == 类型名 且
 *     成员 d 值往返（Array 构造多元素断言 size 与元素）；默认构造零值代表（Int32/String）
 * S2. unPack 模板与解包宏 YomkUnPackPkgT 全分支（错误消息精确断言）：
 *     unPack（null/名不匹配/名匹配但动态类型不符均返回 nullptr，命中返回解包指针）、
 *     服务函数级手写判空返回 eNo + 契约消息、手写判空提前 return（标志位观测）、
 *     YomkUnPackPkgT（运行时类型名，含名匹配但 ClassName 不符的 cast 失败）
 * S3. Event 结构：默认/三参/四参构造、handle() 有无 serviceFunc、m_eventId 初值、m_waitCallback
 * S4. YomkResponse 契约：默认构造、枚举值序（eInvalid=-1/eOk=0/eNo=1）、双参/三参构造
 * S5. 框架结构体默认值（Log::m_level 无类内初始化不作值断言）+ 结构体消息包版 name 就位
 *
 * 说明：全程不调 YOMK_INIT（纯类型/模板测试，不依赖单例）；YomkPkg::name() 公有 setter 用于
 *       伪造类型名以触达 unPack 的 dynamic_pointer_cast 失败分支（名字匹配但实际类型不符）
 *
 * 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
 */

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "YomkPkg.h"

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

int main()
{
    // ============ S1. 标准类型消息包全集（15 标量 + 15 Array = 30 类型） ============
    std::cout << "===== S1. 标准类型消息包全集 =====" << std::endl;
    {
        auto boolPkg = yomkmsg::Bool::shared(true);
        YOMK_TEST_CHECK(boolPkg->name() == "Bool" && boolPkg->d == true, "Bool 构造往返：name 与 d");
        auto charPkg = yomkmsg::Char::shared('A');
        YOMK_TEST_CHECK(charPkg->name() == "Char" && charPkg->d == 'A', "Char 构造往返：name 与 d");
        auto uCharPkg = yomkmsg::UChar::shared(static_cast<unsigned char>(200));
        YOMK_TEST_CHECK(uCharPkg->name() == "UChar" && uCharPkg->d == 200, "UChar 构造往返：name 与 d");
        auto bytePkg = yomkmsg::Byte::shared(static_cast<unsigned char>(201));
        YOMK_TEST_CHECK(bytePkg->name() == "Byte" && bytePkg->d == 201, "Byte 构造往返：name 与 d（与 UChar 同底层但类型名独立）");
        auto int8Pkg = yomkmsg::Int8::shared(static_cast<std::int8_t>(-5));
        YOMK_TEST_CHECK(int8Pkg->name() == "Int8" && int8Pkg->d == -5, "Int8 构造往返：name 与 d");
        auto uint8Pkg = yomkmsg::Uint8::shared(static_cast<std::uint8_t>(250));
        YOMK_TEST_CHECK(uint8Pkg->name() == "Uint8" && uint8Pkg->d == 250, "Uint8 构造往返：name 与 d");
        auto int16Pkg = yomkmsg::Int16::shared(static_cast<std::int16_t>(-300));
        YOMK_TEST_CHECK(int16Pkg->name() == "Int16" && int16Pkg->d == -300, "Int16 构造往返：name 与 d");
        auto uint16Pkg = yomkmsg::Uint16::shared(static_cast<std::uint16_t>(60000));
        YOMK_TEST_CHECK(uint16Pkg->name() == "Uint16" && uint16Pkg->d == 60000, "Uint16 构造往返：name 与 d");
        auto int32Pkg = yomkmsg::Int32::shared(-70000);
        YOMK_TEST_CHECK(int32Pkg->name() == "Int32" && int32Pkg->d == -70000, "Int32 构造往返：name 与 d");
        auto uint32Pkg = yomkmsg::Uint32::shared(4000000000u);
        YOMK_TEST_CHECK(uint32Pkg->name() == "Uint32" && uint32Pkg->d == 4000000000u, "Uint32 构造往返：name 与 d");
        auto int64Pkg = yomkmsg::Int64::shared(-5000000000LL);
        YOMK_TEST_CHECK(int64Pkg->name() == "Int64" && int64Pkg->d == -5000000000LL, "Int64 构造往返：name 与 d");
        auto uint64Pkg = yomkmsg::Uint64::shared(10000000000ULL);
        YOMK_TEST_CHECK(uint64Pkg->name() == "Uint64" && uint64Pkg->d == 10000000000ULL, "Uint64 构造往返：name 与 d");
        auto float32Pkg = yomkmsg::Float32::shared(1.5f);
        YOMK_TEST_CHECK(float32Pkg->name() == "Float32" && float32Pkg->d == 1.5f, "Float32 构造往返：name 与 d（1.5 精确可表示）");
        auto float64Pkg = yomkmsg::Float64::shared(2.25);
        YOMK_TEST_CHECK(float64Pkg->name() == "Float64" && float64Pkg->d == 2.25, "Float64 构造往返：name 与 d");
        auto strPkg = yomkmsg::String::shared(std::string("hello"));
        YOMK_TEST_CHECK(strPkg->name() == "String" && strPkg->d == "hello", "String 构造往返：name 与 d");

        auto boolArrPkg = yomkmsg::BoolArray::shared(std::vector<bool>{true, false, true});
        YOMK_TEST_CHECK(boolArrPkg->name() == "BoolArray" && boolArrPkg->d.size() == 3 && boolArrPkg->d[0] && !boolArrPkg->d[1],
              "BoolArray 构造往返（vector<bool> 位压缩语义）");
        auto charArrPkg = yomkmsg::CharArray::shared(std::vector<char>{'x', 'y'});
        YOMK_TEST_CHECK(charArrPkg->name() == "CharArray" && charArrPkg->d.size() == 2 && charArrPkg->d[0] == 'x',
              "CharArray 构造往返");
        auto uCharArrPkg = yomkmsg::UCharArray::shared(std::vector<unsigned char>{1, 2});
        YOMK_TEST_CHECK(uCharArrPkg->name() == "UCharArray" && uCharArrPkg->d.size() == 2 && uCharArrPkg->d[1] == 2,
              "UCharArray 构造往返");
        auto byteArrPkg = yomkmsg::ByteArray::shared(std::vector<unsigned char>{9, 8});
        YOMK_TEST_CHECK(byteArrPkg->name() == "ByteArray" && byteArrPkg->d.size() == 2 && byteArrPkg->d[0] == 9,
              "ByteArray 构造往返");
        auto int8ArrPkg = yomkmsg::Int8Array::shared(std::vector<signed char>{-1, 2});
        YOMK_TEST_CHECK(int8ArrPkg->name() == "Int8Array" && int8ArrPkg->d.size() == 2 && int8ArrPkg->d[0] == -1,
              "Int8Array 构造往返（底层 vector<signed char>，负值保真）");
        auto uint8ArrPkg = yomkmsg::Uint8Array::shared(std::vector<unsigned char>{250, 251});
        YOMK_TEST_CHECK(uint8ArrPkg->name() == "Uint8Array" && uint8ArrPkg->d.size() == 2 && uint8ArrPkg->d[0] == 250,
              "Uint8Array 构造往返");
        auto int16ArrPkg = yomkmsg::Int16Array::shared(std::vector<std::int16_t>{-1, 2});
        YOMK_TEST_CHECK(int16ArrPkg->name() == "Int16Array" && int16ArrPkg->d.size() == 2 && int16ArrPkg->d[0] == -1,
              "Int16Array 构造往返");
        auto uint16ArrPkg = yomkmsg::Uint16Array::shared(std::vector<std::uint16_t>{1, 2});
        YOMK_TEST_CHECK(uint16ArrPkg->name() == "Uint16Array" && uint16ArrPkg->d.size() == 2 && uint16ArrPkg->d[1] == 2,
              "Uint16Array 构造往返");
        auto int32ArrPkg = yomkmsg::Int32Array::shared(std::vector<std::int32_t>{-1, 2});
        YOMK_TEST_CHECK(int32ArrPkg->name() == "Int32Array" && int32ArrPkg->d.size() == 2 && int32ArrPkg->d[0] == -1,
              "Int32Array 构造往返");
        auto uint32ArrPkg = yomkmsg::Uint32Array::shared(std::vector<std::uint32_t>{1, 2});
        YOMK_TEST_CHECK(uint32ArrPkg->name() == "Uint32Array" && uint32ArrPkg->d.size() == 2 && uint32ArrPkg->d[1] == 2,
              "Uint32Array 构造往返");
        auto int64ArrPkg = yomkmsg::Int64Array::shared(std::vector<std::int64_t>{-1, 2});
        YOMK_TEST_CHECK(int64ArrPkg->name() == "Int64Array" && int64ArrPkg->d.size() == 2 && int64ArrPkg->d[0] == -1,
              "Int64Array 构造往返");
        auto uint64ArrPkg = yomkmsg::Uint64Array::shared(std::vector<std::uint64_t>{1, 2});
        YOMK_TEST_CHECK(uint64ArrPkg->name() == "Uint64Array" && uint64ArrPkg->d.size() == 2 && uint64ArrPkg->d[1] == 2,
              "Uint64Array 构造往返");
        auto float32ArrPkg = yomkmsg::Float32Array::shared(std::vector<float>{0.5f, 1.5f});
        YOMK_TEST_CHECK(float32ArrPkg->name() == "Float32Array" && float32ArrPkg->d.size() == 2 && float32ArrPkg->d[1] == 1.5f,
              "Float32Array 构造往返");
        auto float64ArrPkg = yomkmsg::Float64Array::shared(std::vector<double>{0.25, 0.5});
        YOMK_TEST_CHECK(float64ArrPkg->name() == "Float64Array" && float64ArrPkg->d.size() == 2 && float64ArrPkg->d[0] == 0.25,
              "Float64Array 构造往返");
        auto strArrPkg = yomkmsg::StringArray::shared(std::vector<std::string>{"a", "b"});
        YOMK_TEST_CHECK(strArrPkg->name() == "StringArray" && strArrPkg->d.size() == 2 && strArrPkg->d[1] == "b",
              "StringArray 构造往返");

        auto zeroInt = yomkmsg::Int32();
        YOMK_TEST_CHECK(zeroInt.name() == "Int32" && zeroInt.d == 0, "Int32 默认构造：name 就位且 d 零值");
        auto emptyStr = yomkmsg::String();
        YOMK_TEST_CHECK(emptyStr.name() == "String" && emptyStr.d.empty(), "String 默认构造：name 就位且 d 空");
    }

    // ============ S2. unPack 模板与 YomkUnPackPkgT 全分支 ============
    std::cout << "===== S2. unPack 模板与 YomkUnPackPkgT 全分支 =====" << std::endl;
    {
        // 分支素材：命中包 / 名字不匹配包（公有 setter 伪造）/ 名字匹配但实际类型不符包
        auto okPkg = yomkmsg::String::shared(std::string("hello"));
        auto fakeNamePkg = yomkmsg::String::shared(std::string("x"));
        fakeNamePkg->name("Int32");  // 名字伪造成 Int32（实际仍是 String）
        auto fakeTypePkg = yomkmsg::Int32::shared(1);
        fakeTypePkg->name("String");  // 名字伪造成 String（实际是 Int32，cast 必败）

        // --- unPack + 服务函数级手写判空：失败返回 eNo + 契约消息，命中需手动继续 ---
        auto unpackResp = [](YomkPkgPtr pkg) -> YomkResponse
        {
            auto s = yomk::unPack<yomkmsg::String>(pkg);
            if (!s)
            {
                return {YomkResponse::eNo, "pkg is null or pkg is not String"};
            }
            return YomkResponse(YomkResponse::eOk, s->d);
        };
        YomkResponse nullResp = unpackResp(nullptr);
        YOMK_TEST_CHECK(nullResp.m_status == YomkResponse::eNo && nullResp.m_msg == "pkg is null or pkg is not String",
              "unPack null pkg：服务函数级判空返回 eNo + 契约消息");
        YomkResponse nameMissResp = unpackResp(fakeNamePkg);
        YOMK_TEST_CHECK(nameMissResp.m_status == YomkResponse::eNo && nameMissResp.m_msg == "pkg is null or pkg is not String",
              "unPack 名字不匹配：同款契约消息");
        YomkResponse hitResp = unpackResp(okPkg);
        YOMK_TEST_CHECK(hitResp.m_status == YomkResponse::eOk && hitResp.m_msg == "hello",
              "unPack 命中：解包指针可用且 d 正确");

        // --- unPack + 手写判空提前 return（标志位观测） ---
        bool reached = false;
        auto unpackVoid = [&](YomkPkgPtr pkg)
        {
            auto s = yomk::unPack<yomkmsg::String>(pkg);
            if (!s)
            {
                return;
            }
            reached = true;
        };
        reached = false;
        unpackVoid(nullptr);
        YOMK_TEST_CHECK(reached == false, "unPack null pkg：手写判空提前 return，后续语句未执行");
        reached = false;
        unpackVoid(fakeNamePkg);
        YOMK_TEST_CHECK(reached == false, "unPack 名字不匹配：提前 return");
        std::string voidOut;
        auto unpackVoidHit = [&](YomkPkgPtr pkg)
        {
            auto s = yomk::unPack<yomkmsg::String>(pkg);
            if (!s)
            {
                return;
            }
            voidOut = s->d;
        };
        unpackVoidHit(okPkg);
        YOMK_TEST_CHECK(voidOut == "hello", "unPack 命中：解包指针可用");

        // --- unPack 直接调用：不提前 return（指针 nullptr，调用方手动判空） ---
        auto nullPtr = yomk::unPack<yomkmsg::String>(nullptr);
        YOMK_TEST_CHECK(nullPtr == nullptr, "unPack null pkg：不提前返回，指针为 nullptr");
        auto missPtr = yomk::unPack<yomkmsg::String>(fakeNamePkg);
        YOMK_TEST_CHECK(missPtr == nullptr, "unPack 名字不匹配：指针为 nullptr（后续语句可达）");
        auto hitPtr = yomk::unPack<yomkmsg::String>(okPkg);
        YOMK_TEST_CHECK(hitPtr != nullptr && hitPtr->d == "hello", "unPack 命中：指针非空且 d 正确");

        // --- unPack 名字匹配但动态类型不符：合并为单失败路径，返回 nullptr ---
        auto typeMissPtr = yomk::unPack<yomkmsg::String>(fakeTypePkg);
        YOMK_TEST_CHECK(typeMissPtr == nullptr, "unPack 名匹配但动态类型不符：cast 失败指针 nullptr");

        // --- YomkUnPackPkgT：按运行时类型名（字符串变量）解包 ---
        std::string rtName = "String";
        YomkUnPackPkgT(okPkg, rtName, yomkmsg::String, tHit);
        YOMK_TEST_CHECK(tHit != nullptr && tHit->d == "hello", "UnPackPkgT 运行时名匹配：cast 成功");
        std::string wrongName = "Int32";
        YomkUnPackPkgT(okPkg, wrongName, yomkmsg::String, tMiss);
        YOMK_TEST_CHECK(tMiss == nullptr, "UnPackPkgT 运行时名不匹配：指针 nullptr");
        YomkUnPackPkgT(fakeTypePkg, rtName, yomkmsg::String, tBad);
        YOMK_TEST_CHECK(tBad == nullptr, "UnPackPkgT 名匹配但 ClassName 不符：cast 失败指针 nullptr");
    }

    // ============ S3. Event 结构契约 ============
    std::cout << "===== S3. Event 结构契约 =====" << std::endl;
    {
        yomk::Event ev;
        YOMK_TEST_CHECK(ev.m_eventLoopName.empty() && ev.m_pkg == nullptr && !ev.m_serviceFunc,
              "Event 默认构造：loop 名空/pkg 空/func 空");
        YOMK_TEST_CHECK(ev.m_eventId == 0 && ev.m_tag.empty() && ev.m_response.m_status == YomkResponse::eInvalid && !ev.m_waitCallback,
              "Event 默认构造：eventId 0/tag 空/response eInvalid/waitCallback 空");

        int handled = 0;
        yomk::Event ev3("/L1", yomkmsg::String::shared(std::string("p")),
                        [&handled](YomkPkgPtr) { ++handled; return YomkResponse(YomkResponse::eOk, "handled"); });
        YOMK_TEST_CHECK(ev3.m_eventLoopName == "/L1" && ev3.m_pkg != nullptr && ev3.m_eventId == 0 && ev3.m_tag.empty(),
              "Event 三参构造：字段透传，eventId 保持 0（入队时由框架分配）");
        ev3.handle();
        YOMK_TEST_CHECK(handled == 1 && ev3.m_response.m_status == YomkResponse::eOk && ev3.m_response.m_msg == "handled",
              "Event handle：执行 serviceFunc 并填充 response");

        yomk::Event ev4("/L2", nullptr, YomkServiceFunc(), "tag1");
        YOMK_TEST_CHECK(ev4.m_tag == "tag1" && !ev4.m_serviceFunc, "Event 四参构造：tag 透传，func 可空");

        yomk::Event evSilent;
        evSilent.handle();
        YOMK_TEST_CHECK(evSilent.m_response.m_status == YomkResponse::eInvalid,
              "Event 默认 handle：无 serviceFunc 静默，response 不变");

        bool waited = false;
        ev3.m_waitCallback = [&waited]() { waited = true; };
        ev3.m_waitCallback();
        YOMK_TEST_CHECK(waited, "Event waitCallback：可赋值并执行");
    }

    // ============ S4. YomkResponse 契约 ============
    std::cout << "===== S4. YomkResponse 契约 =====" << std::endl;
    {
        YomkResponse def;
        YOMK_TEST_CHECK(def.m_status == YomkResponse::eInvalid && def.m_msg.empty() && def.m_data == nullptr,
              "YomkResponse 默认构造：eInvalid/msg 空/data 空");
        YOMK_TEST_CHECK(YomkResponse::eInvalid == -1 && YomkResponse::eOk == 0 && YomkResponse::eNo == 1,
              "枚举值序契约：eInvalid=-1/eOk=0/eNo=1");
        YomkResponse two(YomkResponse::eNo, "no msg");
        YOMK_TEST_CHECK(two.m_status == YomkResponse::eNo && two.m_msg == "no msg" && two.m_data == nullptr,
              "双参构造：status/msg 透传，data 默认空");
        auto dataPkg = yomkmsg::String::shared(std::string("payload"));
        YomkResponse three(YomkResponse::eOk, "ok", dataPkg);
        YOMK_TEST_CHECK(three.m_status == YomkResponse::eOk && three.m_data == dataPkg, "三参构造：data 透传");
    }

    // ============ S5. 框架结构体默认值 ============
    std::cout << "===== S5. 框架结构体默认值 =====" << std::endl;
    {
        yomk::Function f;
        YOMK_TEST_CHECK(f.m_funcName.empty() && f.m_msgName.empty() && !f.m_func, "Function 默认：名称/消息名空、func 空");
        yomk::CallFunction cf;
        YOMK_TEST_CHECK(cf.m_funcName.empty() && cf.m_pkg == nullptr, "CallFunction 默认：名称空、pkg 空");
        yomk::Eventloop el;
        YOMK_TEST_CHECK(el.m_eventloopName.empty() && el.m_msgName.empty() && !el.m_defaultServiceFunc,
              "Eventloop 默认：名称/消息名空、默认 func 空");
        yomk::LogFile lf;
        YOMK_TEST_CHECK(lf.m_logger.empty() && lf.m_dir.empty(), "LogFile 默认：logger/dir 空");
        // Log::m_level 为无类内初始化的 POD enum，默认值不稳定，不作值断言
        yomk::Log lg;
        YOMK_TEST_CHECK(lg.m_log.empty() && lg.m_logger.empty() && lg.m_tag.empty(),
              "Log 默认：内容/logger/tag 空（m_level 无默认初始化不断言）");
        yomk::ConsoleLogProxy cp;
        YOMK_TEST_CHECK(!cp.m_consoleLogProxyFunc, "ConsoleLogProxy 默认：代理 func 空");
        yomk::Context ctx;
        YOMK_TEST_CHECK(ctx.m_key.empty() && ctx.m_value == nullptr, "Context 默认：key 空、value 空");
        yomk::ContextChecker cck;
        YOMK_TEST_CHECK(cck.m_key.empty() && !cck.m_checkFunc, "ContextChecker 默认：key 空、check func 空");
        yomk::ContextMonitor cmo;
        YOMK_TEST_CHECK(cmo.m_key.empty() && !cmo.m_contextMonitorFunc && cmo.m_asyncMonitor == false,
              "ContextMonitor 默认：key 空、func 空、async false");
        yomk::VoidPointer vp;
        YOMK_TEST_CHECK(vp.m_ptr == nullptr, "VoidPointer 默认：ptr 空");

        // 结构体消息包版：YomkMsg 宏生成的 name 就位且 d 默认构造
        auto ctxPkg = yomkmsg::Context::shared();
        YOMK_TEST_CHECK(ctxPkg->name() == "Context" && ctxPkg->d.m_key.empty(), "Context 消息包版：name 就位且 d 默认构造");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
