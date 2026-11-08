#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "YomkMsg.h"
#include "YomkPayload.h"

// SDK 核心消息包目录：核心载荷包 + 内置标准类型包（成员名均为 d）。
// 模块专属消息包见 YomkContextMsgs.h / YomkLoggerMsgs.h / YomkEventLoopMsgs.h /
// YomkFunctionPoolMsgs.h / YomkServerInfoMsgs.h；自定义消息经 YomkMsg.h 的
// YOMK_DEFINE_MSG 宏自行声明。

// ============ 核心载荷消息包 ============
YOMK_DEFINE_MSG(yomk::payload::Response, Response, d);
YOMK_DEFINE_MSG(yomk::payload::FuncInfo, FuncInfo, d);
// FunctionPool 全量函数内省（yomk::fn_pool().all() 返回数据；函数总数由 d.size() 承载）
YOMK_DEFINE_MSG(std::vector<yomk::payload::FuncInfo>, FuncInfoList, d);
YOMK_DEFINE_MSG(yomk::payload::VoidPointer, VoidPointer, d);

// 内置标准类型消息包（成员名均为 d）
YOMK_DEFINE_MSG(bool, Bool, d);
YOMK_DEFINE_MSG(std::vector<bool>, BoolArray, d);
YOMK_DEFINE_MSG(signed char, Char, d);
YOMK_DEFINE_MSG(std::vector<char>, CharArray, d);
YOMK_DEFINE_MSG(unsigned char, UChar, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, UCharArray, d);
YOMK_DEFINE_MSG(unsigned char, Byte, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, ByteArray, d);
YOMK_DEFINE_MSG(std::int8_t, Int8, d);
YOMK_DEFINE_MSG(std::vector<signed char>, Int8Array, d);
YOMK_DEFINE_MSG(std::uint8_t, Uint8, d);
YOMK_DEFINE_MSG(std::vector<unsigned char>, Uint8Array, d);
YOMK_DEFINE_MSG(std::int16_t, Int16, d);
YOMK_DEFINE_MSG(std::vector<std::int16_t>, Int16Array, d);
YOMK_DEFINE_MSG(std::uint16_t, Uint16, d);
YOMK_DEFINE_MSG(std::vector<std::uint16_t>, Uint16Array, d);
YOMK_DEFINE_MSG(std::int32_t, Int32, d);
YOMK_DEFINE_MSG(std::vector<std::int32_t>, Int32Array, d);
YOMK_DEFINE_MSG(std::uint32_t, Uint32, d);
YOMK_DEFINE_MSG(std::vector<std::uint32_t>, Uint32Array, d);
YOMK_DEFINE_MSG(std::int64_t, Int64, d);
YOMK_DEFINE_MSG(std::vector<std::int64_t>, Int64Array, d);
YOMK_DEFINE_MSG(std::uint64_t, Uint64, d);
YOMK_DEFINE_MSG(std::vector<std::uint64_t>, Uint64Array, d);
YOMK_DEFINE_MSG(float, Float32, d);
YOMK_DEFINE_MSG(std::vector<float>, Float32Array, d);
YOMK_DEFINE_MSG(double, Float64, d);
YOMK_DEFINE_MSG(std::vector<double>, Float64Array, d);
YOMK_DEFINE_MSG(std::string, String, d);
YOMK_DEFINE_MSG(std::vector<std::string>, StringArray, d);
