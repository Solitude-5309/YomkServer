#pragma once
#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>

// 日志正文格式化：起始预留容量 + 逐参追加的单一实现。
// 独立成头：格式化定版逻辑（bool 输出 true/false、整型手写 itoa、浮点 %g）
// 与 YomkAPI 表面无耦合，外部自行拼日志 content 亦可复用
struct YomkLogFmt
{
    // emit 正文起始预留容量：典型日志单行 64 字节内，预分配减少 string 扩容
    static std::size_t logContentReserve() { static const std::size_t v = 64; return v; }

    // 逐参追加：每个实参前补一个空格，与旧 ostringstream 路径输出逐字节一致。
    // 常用类型直拼，整型手写 itoa，浮点 snprintf，其余可流输出类型回退 ostringstream。
    // bool 刻意输出 true/false（用户定版），不沿用 ostream 默认的 1/0
    static void appendLogArg(std::string& out, const char* v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, const std::string& v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, char v) { out += ' '; out += v; }
    static void appendLogArg(std::string& out, bool v) { out += ' '; out += (v ? "true" : "false"); }
    template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // 手写 itoa：负号 + 十进制数字，与 ostream 默认输出一致。负数 magnitude 在
        // 无符号域取 0 - raw（对最小值取负是 UB）；符号判定 is_signed 编译期短路，
        // 无符号类型不做回读比较
        using U = typename std::make_unsigned<T>::type;
        const U raw = static_cast<U>(v);
        const bool neg = std::is_signed<T>::value &&
                         static_cast<typename std::make_signed<U>::type>(raw) < 0;
        U u = neg ? static_cast<U>(0) - raw : raw;
        constexpr std::size_t kDigitBufSize = 24; // uint64 十进制至多 20 位，24 含余量
        constexpr unsigned kDecBase = 10;
        std::array<char, kDigitBufSize> digits = {};
        std::size_t count = 0;
        do
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            digits[count++] = static_cast<char>('0' + u % kDecBase);
            u /= kDecBase;
        } while (u != 0);
        if (neg)
        {
            out += '-';
        }
        while (count > 0)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
            out += digits[--count];
        }
    }
    template <typename T, typename std::enable_if<std::is_floating_point<T>::value, int>::type = 0>
    static void appendLogArg(std::string& out, T v)
    {
        out += ' ';
        // %g（6 位有效数字）与 ostream 默认输出一致；long double 需 %Lg。
        // 栈缓冲 64 字节足够（最长形如 -1.23457e-308）
        constexpr std::size_t kFltBufSize = 64;
        std::array<char, kFltBufSize> buf = {};
        int n = 0;
        if (std::is_same<T, long double>::value)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            n = std::snprintf(buf.data(), buf.size(), "%Lg", static_cast<long double>(v));
        }
        else
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
            n = std::snprintf(buf.data(), buf.size(), "%g", static_cast<double>(v));
        }
        if (n > 0)
        {
            out.append(buf.data(), static_cast<std::size_t>(n));
        }
    }
    // 回退重载：非算术、且不可转 const char*/std::string 的任意可流输出类型
    // （enable_if 条件与非模板重载集互斥）
    template <typename T,
        typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_convertible<T, const char*>::value
                                    && !std::is_convertible<T, const std::string&>::value,
            int>::type = 0>
    static void appendLogArg(std::string& out, const T& v)
    {
        std::ostringstream oss;
        oss << v;
        out += ' ';
        out += oss.str();
    }
};
