// YomkMsg 消息包体系独立测试（标准类型全集/解包语义全分支的独立覆盖）
//
// 覆盖内容（5 个 Section，约 64 断言）：
// S1. 标准类型消息包全集（15 标量 + 15 Array = 30 类型）：逐类型构造 typeName() == 类型名 且
// 成员 d 值往返（Array 构造多元素断言 size 与元素）；默认构造零值代表（Int32/String）
// S2. unpack 模板与运行时类型名解包全分支（错误消息精确断言）：
// unpack（null/类型名不匹配均返回 nullptr，命中返回解包指针）、
// 服务函数级手写判空返回 ResStatus::Failed + 契约消息、手写判空提前 return（标志位观测）、
// 运行时类型名解包（按包 typeName() 匹配后 dynamic_pointer_cast）
// S3. Event 结构：默认/三参/四参构造、handle() 有无 serviceFunc、eventId 初值、waitCallback
// S4. yomk::payload::Response 契约：默认构造、枚举值序（ResStatus::Invalid=-1/ResStatus::Succeeded=0/ResStatus::Failed=1）、双参/三参构造
// S5. 框架结构体默认值（Log::level 无类内初始化不作值断言）+ 结构体消息包版 typeName 就位
//
// 说明：全程不调 yomk::server().init()（纯类型/模板测试，不依赖单例）；typeName() 由各类型的静态
// kTypeName 返回、不可改写，"类型名匹配但动态类型不符"已非可达分支，故不设该断言
//
// 风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "yomk/ctx/YomkContextMsg.h"
#include "yomk/event_loop/YomkEventLoopMsg.h"
#include "yomk/fn_pool/YomkFunctionPoolMsg.h"
#include "yomk/log/YomkLoggerMsg.h"
#include "yomk/msg/YomkStdMsg.h"

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
        auto boolMsg = yomk::msgs::Bool::shared(true);
        YOMK_TEST_CHECK(boolMsg->typeName() == yomk::msgs::Bool::kTypeName && boolMsg->d == true, "Bool 构造往返：typeName 与 d");
        auto charMsg = yomk::msgs::Char::shared('A');
        YOMK_TEST_CHECK(charMsg->typeName() == yomk::msgs::Char::kTypeName && charMsg->d == 'A', "Char 构造往返：typeName 与 d");
        auto uCharMsg = yomk::msgs::UChar::shared(static_cast<unsigned char>(200));
        YOMK_TEST_CHECK(uCharMsg->typeName() == yomk::msgs::UChar::kTypeName && uCharMsg->d == 200, "UChar 构造往返：typeName 与 d");
        auto byteMsg = yomk::msgs::Byte::shared(static_cast<unsigned char>(201));
        YOMK_TEST_CHECK(byteMsg->typeName() == yomk::msgs::Byte::kTypeName && byteMsg->d == 201, "Byte 构造往返：typeName 与 d（与 UChar 同底层但类型名独立）");
        auto int8Msg = yomk::msgs::Int8::shared(static_cast<std::int8_t>(-5));
        YOMK_TEST_CHECK(int8Msg->typeName() == yomk::msgs::Int8::kTypeName && int8Msg->d == -5, "Int8 构造往返：typeName 与 d");
        auto uint8Msg = yomk::msgs::Uint8::shared(static_cast<std::uint8_t>(250));
        YOMK_TEST_CHECK(uint8Msg->typeName() == yomk::msgs::Uint8::kTypeName && uint8Msg->d == 250, "Uint8 构造往返：typeName 与 d");
        auto int16Msg = yomk::msgs::Int16::shared(static_cast<std::int16_t>(-300));
        YOMK_TEST_CHECK(int16Msg->typeName() == yomk::msgs::Int16::kTypeName && int16Msg->d == -300, "Int16 构造往返：typeName 与 d");
        auto uint16Msg = yomk::msgs::Uint16::shared(static_cast<std::uint16_t>(60000));
        YOMK_TEST_CHECK(uint16Msg->typeName() == yomk::msgs::Uint16::kTypeName && uint16Msg->d == 60000, "Uint16 构造往返：typeName 与 d");
        auto int32Msg = yomk::msgs::Int32::shared(-70000);
        YOMK_TEST_CHECK(int32Msg->typeName() == yomk::msgs::Int32::kTypeName && int32Msg->d == -70000, "Int32 构造往返：typeName 与 d");
        auto uint32Msg = yomk::msgs::Uint32::shared(4000000000u);
        YOMK_TEST_CHECK(uint32Msg->typeName() == yomk::msgs::Uint32::kTypeName && uint32Msg->d == 4000000000u, "Uint32 构造往返：typeName 与 d");
        auto int64Msg = yomk::msgs::Int64::shared(-5000000000LL);
        YOMK_TEST_CHECK(int64Msg->typeName() == yomk::msgs::Int64::kTypeName && int64Msg->d == -5000000000LL, "Int64 构造往返：typeName 与 d");
        auto uint64Msg = yomk::msgs::Uint64::shared(10000000000ULL);
        YOMK_TEST_CHECK(uint64Msg->typeName() == yomk::msgs::Uint64::kTypeName && uint64Msg->d == 10000000000ULL, "Uint64 构造往返：typeName 与 d");
        auto float32Msg = yomk::msgs::Float32::shared(1.5f);
        YOMK_TEST_CHECK(float32Msg->typeName() == yomk::msgs::Float32::kTypeName && float32Msg->d == 1.5f, "Float32 构造往返：typeName 与 d（1.5 精确可表示）");
        auto float64Msg = yomk::msgs::Float64::shared(2.25);
        YOMK_TEST_CHECK(float64Msg->typeName() == yomk::msgs::Float64::kTypeName && float64Msg->d == 2.25, "Float64 构造往返：typeName 与 d");
        auto strMsg = yomk::msgs::String::shared(std::string("hello"));
        YOMK_TEST_CHECK(strMsg->typeName() == yomk::msgs::String::kTypeName && strMsg->d == "hello", "String 构造往返：typeName 与 d");

        auto boolArrMsg = yomk::msgs::BoolArray::shared(std::vector<bool>{true, false, true});
        YOMK_TEST_CHECK(boolArrMsg->typeName() == yomk::msgs::BoolArray::kTypeName && boolArrMsg->d.size() == 3 && boolArrMsg->d[0] && !boolArrMsg->d[1],
              "BoolArray 构造往返（vector<bool> 位压缩语义）");
        auto charArrMsg = yomk::msgs::CharArray::shared(std::vector<char>{'x', 'y'});
        YOMK_TEST_CHECK(charArrMsg->typeName() == yomk::msgs::CharArray::kTypeName && charArrMsg->d.size() == 2 && charArrMsg->d[0] == 'x',
              "CharArray 构造往返");
        auto uCharArrMsg = yomk::msgs::UCharArray::shared(std::vector<unsigned char>{1, 2});
        YOMK_TEST_CHECK(uCharArrMsg->typeName() == yomk::msgs::UCharArray::kTypeName && uCharArrMsg->d.size() == 2 && uCharArrMsg->d[1] == 2,
              "UCharArray 构造往返");
        auto byteArrMsg = yomk::msgs::ByteArray::shared(std::vector<unsigned char>{9, 8});
        YOMK_TEST_CHECK(byteArrMsg->typeName() == yomk::msgs::ByteArray::kTypeName && byteArrMsg->d.size() == 2 && byteArrMsg->d[0] == 9,
              "ByteArray 构造往返");
        auto int8ArrMsg = yomk::msgs::Int8Array::shared(std::vector<signed char>{-1, 2});
        YOMK_TEST_CHECK(int8ArrMsg->typeName() == yomk::msgs::Int8Array::kTypeName && int8ArrMsg->d.size() == 2 && int8ArrMsg->d[0] == -1,
              "Int8Array 构造往返（底层 vector<signed char>，负值保真）");
        auto uint8ArrMsg = yomk::msgs::Uint8Array::shared(std::vector<unsigned char>{250, 251});
        YOMK_TEST_CHECK(uint8ArrMsg->typeName() == yomk::msgs::Uint8Array::kTypeName && uint8ArrMsg->d.size() == 2 && uint8ArrMsg->d[0] == 250,
              "Uint8Array 构造往返");
        auto int16ArrMsg = yomk::msgs::Int16Array::shared(std::vector<std::int16_t>{-1, 2});
        YOMK_TEST_CHECK(int16ArrMsg->typeName() == yomk::msgs::Int16Array::kTypeName && int16ArrMsg->d.size() == 2 && int16ArrMsg->d[0] == -1,
              "Int16Array 构造往返");
        auto uint16ArrMsg = yomk::msgs::Uint16Array::shared(std::vector<std::uint16_t>{1, 2});
        YOMK_TEST_CHECK(uint16ArrMsg->typeName() == yomk::msgs::Uint16Array::kTypeName && uint16ArrMsg->d.size() == 2 && uint16ArrMsg->d[1] == 2,
              "Uint16Array 构造往返");
        auto int32ArrMsg = yomk::msgs::Int32Array::shared(std::vector<std::int32_t>{-1, 2});
        YOMK_TEST_CHECK(int32ArrMsg->typeName() == yomk::msgs::Int32Array::kTypeName && int32ArrMsg->d.size() == 2 && int32ArrMsg->d[0] == -1,
              "Int32Array 构造往返");
        auto uint32ArrMsg = yomk::msgs::Uint32Array::shared(std::vector<std::uint32_t>{1, 2});
        YOMK_TEST_CHECK(uint32ArrMsg->typeName() == yomk::msgs::Uint32Array::kTypeName && uint32ArrMsg->d.size() == 2 && uint32ArrMsg->d[1] == 2,
              "Uint32Array 构造往返");
        auto int64ArrMsg = yomk::msgs::Int64Array::shared(std::vector<std::int64_t>{-1, 2});
        YOMK_TEST_CHECK(int64ArrMsg->typeName() == yomk::msgs::Int64Array::kTypeName && int64ArrMsg->d.size() == 2 && int64ArrMsg->d[0] == -1,
              "Int64Array 构造往返");
        auto uint64ArrMsg = yomk::msgs::Uint64Array::shared(std::vector<std::uint64_t>{1, 2});
        YOMK_TEST_CHECK(uint64ArrMsg->typeName() == yomk::msgs::Uint64Array::kTypeName && uint64ArrMsg->d.size() == 2 && uint64ArrMsg->d[1] == 2,
              "Uint64Array 构造往返");
        auto float32ArrMsg = yomk::msgs::Float32Array::shared(std::vector<float>{0.5f, 1.5f});
        YOMK_TEST_CHECK(float32ArrMsg->typeName() == yomk::msgs::Float32Array::kTypeName && float32ArrMsg->d.size() == 2 && float32ArrMsg->d[1] == 1.5f,
              "Float32Array 构造往返");
        auto float64ArrMsg = yomk::msgs::Float64Array::shared(std::vector<double>{0.25, 0.5});
        YOMK_TEST_CHECK(float64ArrMsg->typeName() == yomk::msgs::Float64Array::kTypeName && float64ArrMsg->d.size() == 2 && float64ArrMsg->d[0] == 0.25,
              "Float64Array 构造往返");
        auto strArrMsg = yomk::msgs::StringArray::shared(std::vector<std::string>{"a", "b"});
        YOMK_TEST_CHECK(strArrMsg->typeName() == yomk::msgs::StringArray::kTypeName && strArrMsg->d.size() == 2 && strArrMsg->d[1] == "b",
              "StringArray 构造往返");

        auto zeroInt = yomk::msgs::Int32();
        YOMK_TEST_CHECK(zeroInt.typeName() == yomk::msgs::Int32::kTypeName && zeroInt.d == 0, "Int32 默认构造：typeName 就位且 d 零值");
        auto emptyStr = yomk::msgs::String();
        YOMK_TEST_CHECK(emptyStr.typeName() == yomk::msgs::String::kTypeName && emptyStr.d.empty(), "String 默认构造：typeName 就位且 d 空");
    }

    // ============ S2. unpack 模板与运行时类型名解包全分支 ============
    std::cout << "===== S2. unpack 模板与运行时类型名解包全分支 =====" << std::endl;
    {
        // 分支素材：命中包 / 类型不符包（真实 Int32，类型名天然不同，无需伪造）
        auto okMsg = yomk::msgs::String::shared(std::string("hello"));
        auto missMsg = yomk::msgs::Int32::shared(1);  // 真实异类型：typeName() 天然返回 Int32 的类型名

        // --- unpack + 服务函数级手写判空：失败返回 ResStatus::Failed + 契约消息，命中需手动继续 ---
        auto unpackResp = [](YomkMsgPtr msg) -> yomk::payload::Response
        {
            auto s = yomk::msgs::unpack<yomk::msgs::String>(msg);
            if (!s)
            {
                return {yomk::payload::ResStatus::Failed, yomk::msgs::typeMismatch(msg, yomk::msgs::String::kTypeName)};
            }
            return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, s->d);
        };
        yomk::payload::Response nullResp = unpackResp(nullptr);
        YOMK_TEST_CHECK(nullResp.status == yomk::payload::ResStatus::Failed && nullResp.msg == std::string("msg is not ") + yomk::msgs::String::kTypeName + ", got 'null'",
              "unpack null msg：服务函数级判空返回 ResStatus::Failed + 具体契约消息(实际类型 null)");
        yomk::payload::Response nameMissResp = unpackResp(missMsg);
        YOMK_TEST_CHECK(nameMissResp.status == yomk::payload::ResStatus::Failed && nameMissResp.msg == std::string("msg is not ") + yomk::msgs::String::kTypeName + ", got '" + yomk::msgs::Int32::kTypeName + "'",
              "unpack 类型名不匹配：具体契约消息(实际类型名)");
        yomk::payload::Response hitResp = unpackResp(okMsg);
        YOMK_TEST_CHECK(hitResp.status == yomk::payload::ResStatus::Succeeded && hitResp.msg == "hello",
              "unpack 命中：解包指针可用且 d 正确");

        // --- unpack + 手写判空提前 return（标志位观测） ---
        bool reached = false;
        auto unpackVoid = [&](YomkMsgPtr msg)
        {
            auto s = yomk::msgs::unpack<yomk::msgs::String>(msg);
            if (!s)
            {
                return;
            }
            reached = true;
        };
        reached = false;
        unpackVoid(nullptr);
        YOMK_TEST_CHECK(reached == false, "unpack null msg：手写判空提前 return，后续语句未执行");
        reached = false;
        unpackVoid(missMsg);
        YOMK_TEST_CHECK(reached == false, "unpack 类型名不匹配：提前 return");
        std::string voidOut;
        auto unpackVoidHit = [&](YomkMsgPtr msg)
        {
            auto s = yomk::msgs::unpack<yomk::msgs::String>(msg);
            if (!s)
            {
                return;
            }
            voidOut = s->d;
        };
        unpackVoidHit(okMsg);
        YOMK_TEST_CHECK(voidOut == "hello", "unpack 命中：解包指针可用");

        // --- unpack 直接调用：不提前 return（指针 nullptr，调用方手动判空） ---
        auto nullPtr = yomk::msgs::unpack<yomk::msgs::String>(nullptr);
        YOMK_TEST_CHECK(nullPtr == nullptr, "unpack null msg：不提前返回，指针为 nullptr");
        auto missPtr = yomk::msgs::unpack<yomk::msgs::String>(missMsg);
        YOMK_TEST_CHECK(missPtr == nullptr, "unpack 类型名不匹配：指针为 nullptr（后续语句可达）");
        auto hitPtr = yomk::msgs::unpack<yomk::msgs::String>(okMsg);
        YOMK_TEST_CHECK(hitPtr != nullptr && hitPtr->d == "hello", "unpack 命中：指针非空且 d 正确");

        // --- 名匹配但动态类型不符分支已不存在：typeName() 直接返回各类型的静态 kTypeName，
        //     名字命中即意味着动态类型就是该类型，无需再断言 cast 失败 ---

        // --- 运行时类型名解包（typeName() 匹配 + dynamic_pointer_cast） ---
        std::string rtName = yomk::msgs::String::kTypeName;
        std::shared_ptr<yomk::msgs::String> tHit = nullptr;
        if (okMsg && okMsg->typeName() == rtName)
        {
            tHit = std::dynamic_pointer_cast<yomk::msgs::String>(okMsg);
        }
        YOMK_TEST_CHECK(tHit != nullptr && tHit->d == "hello", "运行时名匹配：cast 成功");
        std::string wrongName = yomk::msgs::Int32::kTypeName;
        std::shared_ptr<yomk::msgs::String> tMiss = nullptr;
        if (okMsg && okMsg->typeName() == wrongName)
        {
            tMiss = std::dynamic_pointer_cast<yomk::msgs::String>(okMsg);
        }
        YOMK_TEST_CHECK(tMiss == nullptr, "运行时名不匹配：指针 nullptr");
    }

    // ============ S3. Event 结构契约 ============
    std::cout << "===== S3. Event 结构契约 =====" << std::endl;
    {
        yomk::payload::Event ev;
        YOMK_TEST_CHECK(ev.eventLoopName.empty() && ev.msg == nullptr && !ev.serviceFunc,
              "Event 默认构造：loop 名空/msg 空/func 空");
        YOMK_TEST_CHECK(ev.eventId == 0 && ev.tag.empty() && ev.response.status == yomk::payload::ResStatus::Invalid && !ev.waitCallback,
              "Event 默认构造：eventId 0/tag 空/response ResStatus::Invalid/waitCallback 空");

        int handled = 0;
        yomk::payload::Event ev3("/L1", yomk::msgs::String::shared(std::string("p")),
                        [&handled](YomkMsgPtr) { ++handled; return yomk::payload::Response(yomk::payload::ResStatus::Succeeded, "handled"); });
        YOMK_TEST_CHECK(ev3.eventLoopName == "/L1" && ev3.msg != nullptr && ev3.eventId == 0 && ev3.tag.empty(),
              "Event 三参构造：字段透传，eventId 保持 0（入队时由框架分配）");
        ev3.handle();
        YOMK_TEST_CHECK(handled == 1 && ev3.response.status == yomk::payload::ResStatus::Succeeded && ev3.response.msg == "handled",
              "Event handle：执行 serviceFunc 并填充 response");

        yomk::payload::Event ev4("/L2", nullptr, YomkServiceFunc(), "tag1");
        YOMK_TEST_CHECK(ev4.tag == "tag1" && !ev4.serviceFunc, "Event 四参构造：tag 透传，func 可空");

        yomk::payload::Event evSilent;
        evSilent.handle();
        YOMK_TEST_CHECK(evSilent.response.status == yomk::payload::ResStatus::Invalid,
              "Event 默认 handle：无 serviceFunc 静默，response 不变");

        bool waited = false;
        ev3.waitCallback = [&waited]() { waited = true; };
        ev3.waitCallback();
        YOMK_TEST_CHECK(waited, "Event waitCallback：可赋值并执行");
    }

    // ============ S4. yomk::payload::Response 契约 ============
    std::cout << "===== S4. yomk::payload::Response 契约 =====" << std::endl;
    {
        yomk::payload::Response def;
        YOMK_TEST_CHECK(def.status == yomk::payload::ResStatus::Invalid && def.msg.empty() && def.data == nullptr,
              "yomk::payload::Response 默认构造：ResStatus::Invalid/msg 空/data 空");
        YOMK_TEST_CHECK(static_cast<int>(yomk::payload::ResStatus::Invalid) == -1 && static_cast<int>(yomk::payload::ResStatus::Succeeded) == 0 && static_cast<int>(yomk::payload::ResStatus::Failed) == 1,
              "枚举值序契约：ResStatus::Invalid=-1/ResStatus::Succeeded=0/ResStatus::Failed=1");
        yomk::payload::Response two(yomk::payload::ResStatus::Failed, "no msg");
        YOMK_TEST_CHECK(two.status == yomk::payload::ResStatus::Failed && two.msg == "no msg" && two.data == nullptr,
              "双参构造：status/msg 透传，data 默认空");
        auto dataMsg = yomk::msgs::String::shared(std::string("payload"));
        yomk::payload::Response three(yomk::payload::ResStatus::Succeeded, "ok", dataMsg);
        YOMK_TEST_CHECK(three.status == yomk::payload::ResStatus::Succeeded && three.data == dataMsg, "三参构造：data 透传");
    }

    // ============ S5. 框架结构体默认值 ============
    std::cout << "===== S5. 框架结构体默认值 =====" << std::endl;
    {
        yomk::payload::Function f;
        YOMK_TEST_CHECK(f.funcName.empty() && f.msgName.empty() && !f.func, "Function 默认：名称/消息名空、func 空");
        yomk::payload::CallFunction cf;
        YOMK_TEST_CHECK(cf.funcName.empty() && cf.msg == nullptr, "CallFunction 默认：名称空、msg 空");
        yomk::payload::EventLoop el;
        YOMK_TEST_CHECK(el.eventLoopName.empty() && el.msgName.empty() && !el.defaultServiceFunc,
              "EventLoop 默认：名称/消息名空、默认 func 空");
        yomk::payload::LogFile lf;
        YOMK_TEST_CHECK(lf.logger.empty() && lf.dir.empty(), "LogFile 默认：logger/dir 空");
        // Log::level 为无类内初始化的 POD enum，默认值不稳定，不作值断言
        yomk::payload::Log lg;
        YOMK_TEST_CHECK(lg.log.empty() && lg.logger.empty() && lg.tag.empty(),
              "Log 默认：内容/logger/tag 空（level 无默认初始化不断言）");
        yomk::payload::ConsoleLogProxy cp;
        YOMK_TEST_CHECK(!cp.consoleLogProxyFunc, "ConsoleLogProxy 默认：代理 func 空");
        yomk::payload::Context ctx;
        YOMK_TEST_CHECK(ctx.key.empty() && ctx.value == nullptr, "Context 默认：key 空、value 空");
        yomk::payload::ContextChecker cck;
        YOMK_TEST_CHECK(cck.key.empty() && !cck.checkFunc, "ContextChecker 默认：key 空、check func 空");
        yomk::payload::ContextMonitor cmo;
        YOMK_TEST_CHECK(cmo.key.empty() && !cmo.contextMonitorFunc && cmo.asyncMonitor == false,
              "ContextMonitor 默认：key 空、func 空、async false");

        // 结构体消息包版：YOMK_DEFINE_MSG 宏生成的 name 就位且 d 默认构造
        auto ctxMsg = yomk::msgs::Context::shared();
        YOMK_TEST_CHECK(ctxMsg->typeName() == yomk::msgs::Context::kTypeName && ctxMsg->d.key.empty(), "Context 消息包版：typeName 就位且 d 默认构造");
    }

    std::cout << (g_failed == 0 ? "ALL PASSED" : "SOME FAILED") << " (" << g_failed << " failed)" << std::endl;
    return g_failed == 0 ? 0 : 1;
}
