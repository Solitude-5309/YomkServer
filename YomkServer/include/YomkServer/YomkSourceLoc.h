#pragma once
#include <atomic>
#include <cstring>
#include <string>

/**
 * @brief 调用点位置信息（聚合体 + NSDMI）
 * @note 调用点 {} 初始化即在【调用行】完成 file/line/func 捕获; 若改在函数体内求值,
 *     捕获到的是定义处行号
 */
struct YomkSourceLoc
{
    const char* filename = __builtin_FILE();
    unsigned line = __builtin_LINE();
    const char* funcname = __builtin_FUNCTION();

    // 位置信息输出开关（进程级配置，atomic 保证线程安全），log_mgr().location() 分面的
    // show_* 开关转发到这里
    static std::atomic<bool>& fileFlag() { static std::atomic<bool> v(false); return v; }
    static std::atomic<bool>& lineFlag() { static std::atomic<bool> v(true); return v; } // 缺省开
    static std::atomic<bool>& funcFlag() { static std::atomic<bool> v(false); return v; }

    // 取路径 basename（兼容 '/' 与 '\'），与主流日志风格一致
    static const char* baseName(const char* path)
    {
        const char* slash = std::strrchr(path, '/');
        const char* bslash = std::strrchr(path, '\\');
        const char* sep = slash > bslash ? slash : bslash;
        if (sep == nullptr)
        {
            return path;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        return sep + 1;
    }

    /**
     * @brief 按开关将位置前缀 "[file:line:func]" 原地追加到 out；三项全关不追加
     * @note 默认配置（仅行号）输出 "[123]"
     */
    void appendTo(std::string& out) const
    {
        const bool hasFile = fileFlag().load(std::memory_order_relaxed);
        const bool hasLine = lineFlag().load(std::memory_order_relaxed);
        const bool hasFunc = funcFlag().load(std::memory_order_relaxed);
        if (!hasFile && !hasLine && !hasFunc)
        {
            return; // 三项全关：无位置前缀
        }

        out += "[";
        bool first = true;
        auto append = [&out, &first](const std::string& part) {
            if (!first)
            {
                out += ":";
            }
            out += part;
            first = false;
        };
        if (hasFile)
        {
            append(baseName(filename)); // 取 basename，主流日志风格
        }
        if (hasLine)
        {
            append(std::to_string(line));
        }
        if (hasFunc)
        {
            append(funcname);
        }
        out += "]";
    }
};
