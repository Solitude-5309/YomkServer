#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "YomkMsg.h"

// SDK 标准类型消息包目录：内置标量/数组类型（成员名均为 d）。
// 核心载荷消息包见 YomkServerMsgs.h；模块专属消息包见各 Yomk{Module}Msgs.h；
// 自定义消息经 YomkMsg.h 的 YOMK_DEFINE_MSG 宏自行声明。

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
