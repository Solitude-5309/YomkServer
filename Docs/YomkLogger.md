# YomkLogger(日志系统)

框架内置开箱即用的控制台与文件日志输出，支持四级级别开关、自定义 tag、日志代理拦截与日志器自省。随 `YomkAPI::init()` 自动启动。

## API 速查

### 控制台日志

| API | 说明 |
|---|---|
| `YOMK_INFO(...)` / `YOMK_WARN(...)` / `YOMK_ERROR(...)` / `YOMK_DEBUG(...)` | 四级控制台日志 |
| `YOMK_INFO_TAG(tag, ...)` / `YOMK_WARN_TAG(...)` / `YOMK_ERROR_TAG(...)` / `YOMK_DEBUG_TAG(...)` | 带自定义 tag 的四级控制台日志 |
| `YomkAPI::enableConsoleLogDebug()` / `YomkAPI::disableConsoleLogDebug()` | 开关控制台 DEBUG 级别（另有 INFO / WARN / ERROR 同名开关，共四级）；无参 = 全局主闸（框架全局），传日志器名 = 仅门控该器（具名未命中 eNo，不惰性创建） |
| `YomkAPI::setConsoleLogProxy(func)` | 设置控制台日志代理拦截；传 `nullptr` 卸载代理，恢复默认输出 |

### 文件日志

| API | 说明 |
|---|---|
| `YomkAPI::createFileLogger(dir, name)` | 创建文件日志器 |
| `YOMK_FILE_INFO(file, ...)` / `YOMK_FILE_WARN(...)` / `YOMK_FILE_ERROR(...)` / `YOMK_FILE_DEBUG(...)`（`_TAG` 变体传自定义 tag） | 写入指定日志器的内存缓冲（_TAG 变体由日志器统一拼接 [tag] 段） |
| `YomkAPI::enableFileLogDebug(name = "")` / `YomkAPI::disableFileLogDebug(name = "")` | 开关文件日志级别（另有 INFO / WARN / ERROR 同名开关，共四级）；无参 = 文件全局主闸（框架全局），传文件日志器名 = 仅门控该器（具名未命中 eNo，不惰性创建） |
| `YomkAPI::setFileLogProxy(func)` | 设置文件日志代理拦截（回调返回 false 该条不落盘、返回 true 穿透走默认落盘）；传 `nullptr` 或空 std::function 卸载代理，恢复默认落盘 |
| `YomkAPI::flushFileLog(name)` | 显式刷盘 |
| `YomkAPI::deleteFileLogger(name)` | 删除日志器 |

### 日志自省

| API | 说明 |
|---|---|
| `YomkAPI::LOGGER_INFO_LOGGERS(type)` | 按类型获取日志器名列表（type: `"console"`/`"file"`，返回 StringArray 纯名字典序；非法类型 eNo） |
| `YomkAPI::LOGGER_INFO_LOGGER(name, type)` | 按类型单查指定日志器（type: `"console"`/`"file"`；两表允许同名，单表精确查找不 fallback；未注册或非法 type 均 eNo） |
| `YomkAPI::LOGGER_INFO_ALL()` | 查询全部日志器及开关配置状态（LoggerInfoList 清单：元素为单查同款 LoggerInfo，每条内嵌 LoggerState 状态段——console 条目承载控制台逐器门控层真值（未做具名开关恒全 on）且 proxyOn 报控制台代理态，file 条目承载文件逐器门控层真值（未做具名开关恒全 on）且 proxyOn 报文件代理态） |
| `YomkAPI::LOGGER_INFO_GLOBAL(type = "console")` | 查询全局主闸状态（type = "file" 查文件侧、type = "console" 或缺省查控制台侧，两套主闸/两套代理各自独立；未知 type 客户端直返 eNo。返回 LoggerState 独立包：四级别主闸开关 + proxyOn；独立于逐器门控层，与 INFO_ALL/INFO_LOGGER 条目配合完成两层归因） |

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

// 开关指定日志级别（无参 = 全局主闸，框架全局）
YomkAPI::disableConsoleLogDebug();
YomkAPI::enableConsoleLogDebug();
// 逐器门控：仅关断指定日志器（tag 即日志器名），其他日志器输出不受影响
YomkAPI::disableConsoleLogInfo("Network");   // 具名关断
YomkAPI::enableConsoleLogInfo("Network");    // 具名恢复

// 文件日志
YomkAPI::createFileLogger("./logs", "app"); // 创建日志器
YOMK_FILE_INFO("app", "app started");  // 写入内存缓冲
YomkAPI::flushFileLog("app");            // 显式刷盘
YomkAPI::deleteFileLogger("app");           // 删除日志器

// 文件日志开关（无参 = 文件全局主闸，具名 = 逐器门控；两层 AND 后才写入）
YomkAPI::disableFileLogDebug();       // 文件主闸关断：全部文件器 DEBUG 均不写入
YomkAPI::enableFileLogDebug();
YomkAPI::disableFileLogInfo("app");   // 具名关断：仅 app 的 INFO 不再写入，其他文件器不受影响
YomkAPI::enableFileLogInfo("app");

// 控制台日志拦截/代理
YomkAPI::setConsoleLogProxy([](const yomk::Log &log) {
    if (log.m_level == yomk::Log::eDebug)
        return false; // 返回 false 表示消费此日志，不再默认输出
    return true;      // 返回 true 则继续走框架默认打印
});
YomkAPI::setConsoleLogProxy(nullptr); // 传 nullptr 卸载代理，恢复默认输出

// 文件日志代理：返回 false 拦截该条不落盘，返回 true 穿透走默认落盘
YomkAPI::setFileLogProxy([](const yomk::Log &log) {
    if (log.m_level == yomk::Log::eDebug)
        return false; // 返回 false 表示拦截此条，不写入文件
    return true;      // 返回 true 则继续走框架默认落盘
});
YomkAPI::setFileLogProxy(nullptr); // 传 nullptr 卸载文件代理，恢复默认落盘
```

## 注意事项

- tag 即控制台日志器名，首次使用自动创建。
- 级别开关两级并联：无参作用于全局主闸（框架全局），具名作用于单个日志器（逐器门控）；输出/落盘取两层 AND。控制台开关（enable/disableConsoleLog*）只影响控制台输出，文件开关（enable/disableFileLog*）只影响文件写入，两套开关相互独立。
- 内省归因：INFO_ALL/INFO_LOGGER 条目报逐器门控层真值（console/file 条目各自独立，未做具名开关恒全 on；proxyOn 各报对应侧代理态）；条目 on 但无输出/未落盘时查 `YomkAPI::LOGGER_INFO_GLOBAL("console"/"file")` 确认对应侧主闸是否关断——两层状态各自真实呈现，可唯一归因。
- `YomkAPI::flushFileLog` 显式刷盘；日志器释放时亦会自动落盘。
- `YomkAPI::deleteFileLogger` 同名清 console + file 两表；不删磁盘 `.log` 文件。
- 代理回调返回 `false` 表示消费此日志不再默认输出（控制台代理）或不再落盘（文件代理）；返回 `true` 继续框架默认行为。控制台代理与文件代理相互独立，传 `nullptr` 或空 std::function 即卸载。

## 完整示例

完整可运行演示见 `Examples/ExampleYomkLogger.cpp`（6 个机械臂产线场景小示例 showArmXXX 覆盖全部日志 API——含两级开关与双代理演示，读运行输出即可理解每个调用）。
