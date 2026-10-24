# YomkLogger(日志系统)

框架内置开箱即用的控制台与文件日志输出，支持四级级别开关、自定义 tag、日志代理拦截与日志器自省。随 `YOMK_INIT()` 自动启动。

## API 速查

### 控制台日志

| API | 说明 |
|---|---|
| `YOMK_INFO(...)` / `YOMK_WARN(...)` / `YOMK_ERROR(...)` / `YOMK_DEBUG(...)` | 四级控制台日志 |
| `YOMK_INFO_TAG(tag, ...)` / `YOMK_WARN_TAG(...)` / `YOMK_ERROR_TAG(...)` / `YOMK_DEBUG_TAG(...)` | 带自定义 tag 的四级控制台日志 |
| `YOMK_ON_CONSOLE_LOG_DEBUG()` / `YOMK_OFF_CONSOLE_LOG_DEBUG()` | 开关控制台 DEBUG 级别（另有 INFO / WARN / ERROR 同名开关，共四级） |
| `YOMK_SET_CONSOLE_LOG_PROXY(func)` | 设置控制台日志代理拦截；传 `nullptr` 卸载代理，恢复默认输出 |

### 文件日志

| API | 说明 |
|---|---|
| `YOMK_FILE_LOG_CREATE(dir, name)` | 创建文件日志器 |
| `YOMK_FILE_INFO(file, ...)` / `YOMK_FILE_WARN(...)` / `YOMK_FILE_ERROR(...)` / `YOMK_FILE_DEBUG(...)` | 写入指定日志器的内存缓冲 |
| `YOMK_FILE_LOG_WRITE(name)` | 显式刷盘 |
| `YOMK_FILE_LOG_DELETE(name)` | 删除日志器 |

### 日志自省

| API | 说明 |
|---|---|
| `YOMK_LOGGER_INFO_LOGGERS(type)` | 按类型获取日志器名列表（type: `"console"`/`"file"`，返回 StringArray 纯名字典序；非法类型 eNo） |
| `YOMK_LOGGER_INFO_LOGGER(name, type)` | 按类型单查指定日志器（type: `"console"`/`"file"`；两表允许同名，单表精确查找不 fallback；未注册或非法 type 均 eNo） |
| `YOMK_LOGGER_INFO_ALL()` | 查询全部日志器及开关配置状态（LoggerInfoList 清单：元素为单查同款 LoggerInfo，每条内嵌 ConsoleLogState 状态段——console 条目为真实全局状态，file 条目四级别恒 true、proxyOn 恒 false） |

## 使用示例

```cpp
// 控制台日志
YOMK_INFO("server port:", 8080);
YOMK_WARN("resource running low");
YOMK_ERROR("failed to connect");
YOMK_DEBUG("trace id:", 12345);
// 指定 Tag：
YOMK_INFO_TAG("Network", "connected to remote host");
// 输出格式：[时间] [级别] [tag] [行号] 内容；tag 即控制台日志器名，首次使用自动创建

// 开关指定日志级别
YOMK_OFF_CONSOLE_LOG_DEBUG();
YOMK_ON_CONSOLE_LOG_DEBUG();

// 文件日志
YOMK_FILE_LOG_CREATE("./logs", "app"); // 创建日志器
YOMK_FILE_INFO("app", "app started");  // 写入内存缓冲
YOMK_FILE_LOG_WRITE("app");            // 显式刷盘
YOMK_FILE_LOG_DELETE("app");           // 删除日志器

// 控制台日志拦截/代理
YOMK_SET_CONSOLE_LOG_PROXY([](const yomk::Log &log) {
    if (log.m_level == yomk::Log::eDebug)
        return false; // 返回 false 表示消费此日志，不再默认输出
    return true;      // 返回 true 则继续走框架默认打印
});
YOMK_SET_CONSOLE_LOG_PROXY(nullptr); // 传 nullptr 卸载代理，恢复默认输出
```

## 注意事项

- tag 即控制台日志器名，首次使用自动创建。
- 级别开关为框架全局，只影响控制台输出；文件日志四级全记。
- `YOMK_FILE_LOG_WRITE` 显式刷盘；日志器释放时亦会自动落盘。
- `YOMK_FILE_LOG_DELETE` 同名清 console + file 两表；不删磁盘 `.log` 文件。
- 代理回调返回 `false` 表示消费此日志不再默认输出；返回 `true` 继续框架默认打印。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkLogger.cpp`（8 步覆盖全部日志 API，读运行输出即可理解每个调用）。
