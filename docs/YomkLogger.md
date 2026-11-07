# YomkLogger（日志系统）

框架内置开箱即用的控制台与文件日志输出：六级级别开关（Trace / Debug / Info / Warn / Error / Fatal，严重度升序）、自定义 tag、日志代理拦截、位置前缀开关与日志器自省。随 `YomkApi.server().init()` 自动启动。能力分两个域入口：**发射**走 `YomkApi.log()`（写一条日志），**管理**走 `YomkApi.log_mgr()`（门控 / 代理 / 前缀 / 文件日志器生命周期 / 内省）。

## 发射链（YomkApi.log()）

`info / warn / error / debug / trace / fatal` 六个级别方法 × 变参（参数逐个追加拼接输出，`bool` 输出 `true/false`），终结返回三态 `ResStatus`（未初始化 `Invalid`）。域入口在**调用行**捕获位置前缀，链须单表达式内联使用、不可缓存。

| 链 | 行为 |
|---|---|
| `YomkApi.log().info(*args)` | 主控制台输出，tag 缺省 `main`（tag 即控制台日志器名，首次使用自动创建） |
| `YomkApi.log().tag(t).info(*args)` | 指定 tag |
| `YomkApi.log().to_sink(name).info(*args)` | 写入具名文件日志器（六级别方法同理）；`.tag(t)` 可叠加；未知名返回 `Failed`，**不惰性创建**——文件日志器须先经 `log_mgr().dir(d).file(name).create()` 创建 |

输出行格式：`[YYYY-MM-DD HH:MM:SS.mmm] [级别] [tag] [位置前缀] 内容`，级别标签定宽 5（如 `[INFO ]`）保证纵向对齐；位置前缀默认仅行号：

```text
[2026-11-07 10:00:00.000] [INFO ] [main] [42] server port: 8080
```

## 管理链（YomkApi.log_mgr()）速查

### 级别门控（console / file 两套完全独立；输出/落盘取两级 AND）

| 链 | 说明 |
|---|---|
| `YomkApi.log_mgr().console(name).level(lv).enable()` / `.disable()` | 控制台门控；`name` 省略/空 = 全局主闸（框架全局），具名 = 仅该日志器（逐器门控，未命中 `Failed`，不惰性创建）；`.level(lv)` 缺省 `Info`（`Log.eTrace` … `Log.eFatal` 六级之一） |
| `YomkApi.log_mgr().file(name).level(lv).enable()` / `.disable()` | 文件侧同理：无具名 = 文件全局主闸，具名 = 逐器门控 |

### 代理拦截与位置前缀

| 链 | 说明 |
|---|---|
| `YomkApi.log_mgr().console().proxy(fn).apply()` | 控制台代理：回调收 `Log` 包（`m_level / m_log / m_logger / m_tag` 字段），返回 `False` 消费该条不再默认输出、`True` 穿透走框架打印；传 `None` 卸载恢复默认 |
| `YomkApi.log_mgr().file().proxy(fn).apply()` | 文件代理同理（`False` 该条不落盘）；与控制台代理相互独立 |
| `YomkApi.log_mgr().location().show_file(on).show_line(on).show_func(on).apply()` | 位置前缀三开关（进程级，控制台与文件同时生效；一次性落定，`apply()` 恒 `Succeeded`）。前缀按 `[file:line:func]` 以 `:` 拼接，关断的段整体省略：默认仅行号 `[N]`；开文件+行号 `[main.py:N]`；三项全关无前缀 |

### 文件日志器生命周期

| 链 | 说明 |
|---|---|
| `YomkApi.log_mgr().dir(d).file(name).create()` | 创建文件日志器（落盘为 `d/name.log`）；`.dir()` 须先于 `.file()` 累积；空名/空目录 `Invalid`，同名重复创建 `Failed` |
| `YomkApi.log_mgr().file(name).flush()` | 将该日志器内存缓冲落盘；未知名 `Failed` |
| `YomkApi.log_mgr().file(name).remove()` | 删除日志器（同名 console + file 两表一并清除，两表皆未命中 `Failed`；**不删除磁盘 `.log` 文件**）；释放时未落盘缓冲自动落盘 |

### 内省

| 链 | 说明 |
|---|---|
| `YomkApi.log_mgr().console().list()` / `.file().list()` | 该类型全部日志器名（`data` 为字典序名字列表） |
| `YomkApi.log_mgr().console(name).get_info()` / `.file(name).get_info()` | 单器状态 `LoggerInfo`（`logger_name / type / dir / logger_state`；两表允许同名，须按类型精确单查；未注册 `Failed`） |
| `YomkApi.log_mgr().console().status()` / `.file().status()` | 全局主闸状态 `LoggerState`（六级别开关 `trace_on…fatal_on` + `proxy_on`；与逐器门控层相互独立） |
| `YomkApi.log_mgr().all()` | 全部日志器及逐器门控状态（每条内嵌 `LoggerState` 状态段为该器门控层真值） |

## 使用示例

```python
import YomkApi
from YomkServerPy import Log

YomkApi.server().init()

# 控制台发射
YomkApi.log().info("server port:", 8080)
YomkApi.log().tag("Network").warn("resource running low")
YomkApi.log().error("failed to connect:", False)          # bool 输出 false

# 文件日志：先创建，再经 to_sink 写入，最后显式刷盘
YomkApi.log_mgr().dir("./logs").file("app").create()
YomkApi.log().to_sink("app").info("app started")
YomkApi.log_mgr().file("app").flush()

# 门控：关控制台全局 DEBUG，再单独恢复 Network 器的 DEBUG（两级 AND 都开才输出）
YomkApi.log_mgr().console().level(Log.eDebug).disable()
YomkApi.log_mgr().console("Network").level(Log.eDebug).enable()

# 代理：拦截全部 Debug 不进控制台
YomkApi.log_mgr().console().proxy(
    lambda log: log.m_level != Log.eDebug                 # False 消费该条
).apply()
YomkApi.log_mgr().console().proxy(None).apply()           # 卸载代理，恢复默认

# 位置前缀：文件+行号+函数
YomkApi.log_mgr().location().show_file(True).show_func(True).apply()

# 清理
YomkApi.log_mgr().file("app").remove()
```

## 注意事项

- 发射链是**同步阻塞**的：内部经请求域投递至 Logger 内置服务，返回 `ResStatus` 表示该条已被受理（门控/代理在受理内生效）；高频路径可先门控再发射。
- 两级门控并联：全局主闸（`console()/file()` 空名形态）与逐器门控（具名形态）做 AND；控制台开关只影响控制台、文件开关只影响文件。具名门控未命中日志器返回 `Failed`，不惰性创建。
- 内省归因：`get_info` / `all()` 条目报**逐器门控层**真值（未做具名开关恒全 on）；条目 on 但无输出/未落盘时，查 `console()/file().status()` 确认对应侧**全局主闸**——两层状态各自独立，可唯一归因。
- tag 即控制台日志器名、首次使用自动创建；缺省名 `main`（框架预创建，无 tag 门面的日志归属它）；文件日志器必须先 `create()`，向未知名发射返回 `Failed`。
- 代理回调返回 `False` = 消费该条（控制台不再输出 / 文件不落盘），`True` = 穿透走框架默认行为；控制台与文件代理各自独立，`None` 卸载。
- `log()` 域入口在调用行捕获位置信息，链须单表达式内联，缓存 builder 会导致位置错误。
- 未知级别数值（如 `Log(99, ...)` 经裸链投递）降级 Info 输出，与 C++ default 分支一致。

## 完整示例

完整可运行演示见 `examples/ExampleYomkLogger.py`（6 个机械臂产线场景覆盖全部日志 API——含两级开关与双代理演示，读运行输出即可理解每个调用）。
