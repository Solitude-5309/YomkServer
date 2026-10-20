#!/usr/bin/env python3
"""
ExampleYomkLogger —— YomkLogger 日志系统快速上手示例（面向初学者）

本示例按 8 个步骤演示日志系统的全部用法。每一步先打印横幅说明
"接下来做什么、预期看到什么"，再调用门面 API，随后用框架日志与内省
结果自证行为——只读运行输出即可明白每个 API 调用发生了什么。

步骤总览：
1. 控制台日志四级别（默认 tag）
2. 自定义 Tag（tag 随记录显示，兼容双填兼作日志器名）
3. 级别开关（全局，只影响控制台）
4. 控制台日志代理（消费与放行两种返回值）
5. 卸载代理（恢复默认输出）
6. 文件日志（创建、写入、刷盘、回读验证）
7. 内省三件套（清单 / 单查 / 全量状态）
8. 删除日志器（两表清理、磁盘文件保留、惰性重建）

涉及的门面 API（YomkApi）：
  版本：  YomkApi.__version__
  控制台：console_log_info/warn/error/debug_tag（默认 tag 传 "MainLogger"）
  开关：  on/off_console_log_info/warn/error/debug
  代理：  set_console_log_proxy
  文件：  file_log_create、file_log_info/warn/error/debug_tag、file_log_write
  内省：  logger_info_loggers、logger_info_logger、logger_info_all
  删除：  file_log_delete

与 main（C++ 宏）的映射口径：
- C++ 无 tag 宏（YOMK_INFO 等）等价于 tag="MainLogger"，python 侧显式传递；
- python 门面同样在内容开头注入 "[行号]" 定位调用处；
- 横幅与代理前缀走 print 而非框架日志：级别关闭/代理拦截时叙事仍然可见，
  这样"被吞掉的日志"才能被解释。
"""

from pathlib import Path

import YomkApi
from YomkServerPy import Log


def print_step(n, title, explain):
    print(f"\n====== 步骤{n}：{title} ======")
    print(f">> {explain}")


def print_resp(prefix, resp):
    # YomkResponse 三态：eOk=0 成功；eNo=1 不存在或被拒绝；eInvalid=-1 参数无效或未初始化
    print(f'[{prefix}] status={resp.status.value}, msg="{resp.msg}"')


def dump_lines(prefix, resp):
    print_resp(prefix, resp)
    if resp.data is None:
        print(">> (no data)")
        return
    for line in resp.data:
        print(f">> | {line}")


def console_log_proxy(log: Log) -> bool:
    """自定义控制台日志代理：框架每写一条控制台日志，都会先调用本函数。"""
    # 演示"消费"：DEBUG 日志被代理吞掉，不再出现在控制台
    if log.m_level == Log.eDebug:
        return False
    # 演示"放行"：其余级别加自定义前缀后交给框架默认输出
    print(f"[LogProxy] {log.m_log} (tag={log.m_tag})")
    return True


def main():
    # 初始化框架（自动启动内置 YomkLogger 服务）
    YomkApi.init()

    print_step(0, "框架就绪", "YomkApi.init 已启动内置日志服务；YomkApi.__version__ 返回框架版本号。")
    print_resp("YomkApi.__version__",
               YomkApi.YomkResponse(YomkApi.ResStatus.eOk, YomkApi.__version__))

    # 日志目录：示例文件同目录下的 YomkLog/（步骤6 创建文件日志器使用）
    log_dir = Path(__file__).resolve().parent / "YomkLog"

    #
    # 步骤1：控制台日志四级别（默认 tag）
    #
    # C++ 无 tag 宏 YOMK_INFO/WARN/ERROR/DEBUG 使用默认日志器名 "MainLogger"。
    # 控制台输出格式四段：[时间] [级别] [tag] [行号] 内容，
    # 例如：[2026-10-20 15:00:00.123] [Info ] [MainLogger] [91] hello
    # （门面与 main 宏一致，自动把 "[行号]" 拼到内容开头，方便定位调用处。）
    #
    print_step(1, "控制台日志四级别（默认 tag）",
               "下面 4 条按 INFO/WARN/ERROR/DEBUG 输出，注意级别字段与 tag 定位。")
    print_resp("console_log_info_tag",
               YomkApi.console_log_info_tag("MainLogger", "hello yomk logger, this is", "info"))
    print_resp("console_log_warn_tag",
               YomkApi.console_log_warn_tag("MainLogger", "low disk space, free=", 1024))
    print_resp("console_log_error_tag",
               YomkApi.console_log_error_tag("MainLogger", "connect refused, code=", -1))
    print_resp("console_log_debug_tag",
               YomkApi.console_log_debug_tag("MainLogger", "trace id:", 12345))

    #
    # 步骤2：自定义 Tag
    #
    # console_log_*_tag(tag, ...) 的 tag 作为日志标签随记录显示（Log.m_tag），
    # 兼容双填兼作日志器名（Log.m_logger）——首次使用时框架自动创建同名日志器（无需手动注册）。
    # 用内省 logger_info_loggers 清单自证：能看到 "MainLogger [console]" 和 "user.service [console]"。
    #
    print_step(2, "自定义 Tag", "tag 随记录显示、兼容双填兼作日志器名，首次使用自动创建；随后打印日志器清单自证。")
    print_resp("console_log_info_tag", YomkApi.console_log_info_tag(
        "user.service", "user login, id=", 7))
    print_resp("console_log_warn_tag", YomkApi.console_log_warn_tag(
        "user.service", "retry count=", 2))
    print_resp("console_log_error_tag", YomkApi.console_log_error_tag(
        "user.service", "session expired"))
    print_resp("console_log_debug_tag", YomkApi.console_log_debug_tag(
        "user.service", "cache hit, key=token"))
    dump_lines("logger_info_loggers", YomkApi.logger_info_loggers())

    #
    # 步骤3：级别开关（全局，只影响控制台）
    #
    # 4 个 off 逐级关闭控制台输出——注意级别是框架全局开关，
    # 不区分日志器；关闭后对应的日志调用仍返回 eOk，只是不再打印。
    # 文件日志不受影响（步骤6 会回指这一点）。
    #
    print_step(3, "级别开关（全局，只影响控制台）", "先全关：接下来 4 条日志一行都不会出现（调用仍成功返回 eOk）。")
    print_resp("off_console_log_info", YomkApi.off_console_log_info())
    print_resp("off_console_log_warn", YomkApi.off_console_log_warn())
    print_resp("off_console_log_error", YomkApi.off_console_log_error())
    print_resp("off_console_log_debug", YomkApi.off_console_log_debug())

    print(">> 上面一行框架日志都没有？——四个级别已全关，调用本身仍返回 eOk")
    print_resp("console_log_info_tag(被关闭)", YomkApi.console_log_info_tag(
        "MainLogger", "you cannot see me: info is off"))
    print_resp("console_log_warn_tag(被关闭)", YomkApi.console_log_warn_tag(
        "MainLogger", "you cannot see me: warn is off"))
    print_resp("console_log_error_tag(被关闭)", YomkApi.console_log_error_tag(
        "MainLogger", "you cannot see me: error is off"))
    print_resp("console_log_debug_tag(被关闭)", YomkApi.console_log_debug_tag(
        "MainLogger", "you cannot see me: debug is off"))

    # 用内省 ALL 自证开关状态：首行 console:debug:off info:off warn:off error:off proxy:off
    dump_lines("logger_info_all(全关)", YomkApi.logger_info_all())

    print_step(3, "级别开关（续）：恢复", "4 个 on 重新打开，同样的日志立刻出现。")
    print_resp("on_console_log_info", YomkApi.on_console_log_info())
    print_resp("on_console_log_warn", YomkApi.on_console_log_warn())
    print_resp("on_console_log_error", YomkApi.on_console_log_error())
    print_resp("on_console_log_debug", YomkApi.on_console_log_debug())
    print_resp("console_log_info_tag(已恢复)",
               YomkApi.console_log_info_tag("MainLogger", "info is back"))
    print_resp("console_log_debug_tag(已恢复)",
               YomkApi.console_log_debug_tag("MainLogger", "debug is back"))

    #
    # 步骤4：控制台日志代理（消费与放行）
    #
    # 安装 console_log_proxy 后所有控制台日志先过代理：
    # - DEBUG 被 return False 消费掉——控制台不再出现；
    # - 其余被加 [LogProxy] 前缀后 return True 放行——正常输出。
    # 内省 ALL 首行 proxy:on 标记代理已安装。
    #
    print_step(
        4, "控制台日志代理", "代理消费 DEBUG（不再输出）、放行其他级别（加 [LogProxy] 前缀）；观察下面 4 条的差异。")
    print_resp("set_console_log_proxy",
               YomkApi.set_console_log_proxy(console_log_proxy))
    print(">> DEBUG 这条将被代理吞掉，不会出现；其余 3 条带 [LogProxy] 前缀")
    print_resp("console_log_info_tag(代理放行)", YomkApi.console_log_info_tag(
        "MainLogger", "passed through proxy"))
    print_resp("console_log_warn_tag(代理放行)", YomkApi.console_log_warn_tag(
        "MainLogger", "passed through proxy"))
    print_resp("console_log_error_tag(代理放行)", YomkApi.console_log_error_tag(
        "MainLogger", "passed through proxy"))
    print_resp("console_log_debug_tag(代理消费)", YomkApi.console_log_debug_tag(
        "MainLogger", "consumed by proxy, never printed"))
    dump_lines("logger_info_all(代理开启)", YomkApi.logger_info_all())

    #
    # 步骤5：卸载代理
    #
    # 传 None 即卸载，恢复框架默认输出格式（无 [LogProxy] 前缀），
    # 内省首行 proxy 回到 off。
    #
    print_step(5, "卸载代理", "传 None 恢复默认输出；对比上一条 [LogProxy] 前缀消失。")
    print_resp("set_console_log_proxy(None)",
               YomkApi.set_console_log_proxy(None))
    print_resp("console_log_info_tag(默认输出)", YomkApi.console_log_info_tag(
        "MainLogger", "default format again, no proxy prefix"))
    dump_lines("logger_info_all(代理已卸载)", YomkApi.logger_info_all())

    #
    # 步骤6：文件日志
    #
    # 文件日志器独立于控制台：先 file_log_create（目录 + 日志器名），
    # 再用 file_log_*_tag 写入内存缓冲，最后 file_log_write 显式刷盘到 dir/name.log。
    # 注意：级别开关只管控制台——即使步骤3 全关过，文件日志四级全记。
    #
    print_step(
        6, "文件日志", "创建 app 日志器，写 8 条（默认 tag + 自定义 tag），刷盘后回读文件验证；文件日志不受级别开关影响。")
    print_resp("file_log_create", YomkApi.file_log_create(str(log_dir), "app"))
    # 默认 tag：与 C++ 无 tag 宏一致传 "MainLogger"
    print_resp("file_log_info_tag", YomkApi.file_log_info_tag(
        "app", "MainLogger", "file log info, order=", 1))
    print_resp("file_log_warn_tag", YomkApi.file_log_warn_tag(
        "app", "MainLogger", "file log warn, order=", 2))
    print_resp("file_log_error_tag", YomkApi.file_log_error_tag(
        "app", "MainLogger", "file log error, order=", 3))
    print_resp("file_log_debug_tag", YomkApi.file_log_debug_tag(
        "app", "MainLogger", "file log debug, order=", 4))
    # 自定义 tag：走 Log.m_tag 字段由日志器统一拼接，便于在文件里区分模块
    print_resp("file_log_info_tag", YomkApi.file_log_info_tag(
        "app", "biz", "biz event, order=", 5))
    print_resp("file_log_warn_tag", YomkApi.file_log_warn_tag(
        "app", "biz", "biz warning, order=", 6))
    print_resp("file_log_error_tag", YomkApi.file_log_error_tag(
        "app", "biz", "biz failure, order=", 7))
    print_resp("file_log_debug_tag", YomkApi.file_log_debug_tag(
        "app", "biz", "biz detail, order=", 8))
    print_resp("file_log_write", YomkApi.file_log_write("app"))

    # 回读 .log 文件：眼见为实，8 条全部在磁盘上
    log_file_path = log_dir / "app.log"
    print(f">> ---- 回读 {log_file_path} ----")
    with open(log_file_path, encoding="utf-8") as log_file:
        for line in log_file:
            print(">> | " + line.rstrip("\n"))

    #
    # 步骤7：内省三件套
    #
    # logger_info_loggers：日志器清单（console 段 + file 段，各自按名称字典序）；
    # logger_info_logger(name)：单查，命中返回元信息行，未注册返回 eNo=1；
    # logger_info_all：全量状态，首行为控制台级别与代理开关。
    #
    print_step(7, "内省三件套", "清单看全部、单查看一个、ALL 看总状态；单查一个从未创建的名字演示 eNo 惯例。")
    dump_lines("logger_info_loggers", YomkApi.logger_info_loggers())
    print_resp("logger_info_logger(app)", YomkApi.logger_info_logger("app"))
    print_resp("logger_info_logger(ghost)",
               YomkApi.logger_info_logger("ghost"))
    print(">> ghost 未注册 → eNo=1，not-found 是框架统一惯例")
    dump_lines("logger_info_all", YomkApi.logger_info_all())

    #
    # 步骤8：删除日志器
    #
    # file_log_delete 按名字同时清理 console/file 两张表：
    # - "app" 只在 file 表 → "deleted console:0 file:1"；
    #   删除时未刷盘的缓冲会自动落盘，但磁盘 .log 文件保留（数据保全）；
    # - "ghost" 两表均未命中 → eNo=1；
    # - "MainLogger" 可删（console:1 file:0），删除后再写日志会自动重建。
    #
    print_step(
        8, "删除日志器", "删除 app / ghost / MainLogger 观察返回值；删除后磁盘 .log 仍在，MainLogger 再写自动重建。")
    print_resp("file_log_delete(app)", YomkApi.file_log_delete("app"))
    print_resp("file_log_delete(ghost)", YomkApi.file_log_delete("ghost"))
    print_resp("file_log_delete(MainLogger)",
               YomkApi.file_log_delete("MainLogger"))

    # 磁盘文件保留验证：app.log 仍可读
    print(f">> ---- 删除后回读 {log_file_path}（磁盘文件保留） ----")
    with open(log_file_path, encoding="utf-8") as log_file:
        first_line = log_file.readline()
        if first_line:
            print(">> | " + first_line.rstrip("\n") + " ...（内容仍在）")

    # MainLogger 惰性重建：删除后首次写日志，框架自动重建同名日志器
    print_resp("console_log_info_tag(重建后)", YomkApi.console_log_info_tag(
        "MainLogger", "MainLogger recreated on demand"))
    dump_lines("logger_info_loggers(收尾)", YomkApi.logger_info_loggers())

    print("\n====== 示例结束：按回车退出 ======")
    input()


if __name__ == "__main__":
    main()
