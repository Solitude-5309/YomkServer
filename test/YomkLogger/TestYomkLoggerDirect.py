#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
TestYomkLoggerDirect.py —— ConsoleLogger / FileLogger 类直连测试（对齐 main
Test/YomkLogger/TestYomkLoggerDirect.cpp，LG1 轮交付）

覆盖内容：
1. ConsoleLogger 直连：默认名 MainLogger、setName/getName 往返、四级别输出行格式 +
   时间戳格式、非法级别 99 default 错误提示 + 降级 Info、4096 超长名 + 空内容、
   tag 显示语义（非空 [tag] 优先 / 空 tag 回退 [日志器名]）
2. FileLogger 直连：默认名/setter、init 建多级目录 + 空 .log 返 bool、log 四级别
   内存缓冲（write 前文件 0 字节）、write 追加落盘 + 清缓冲 + 二次 write no-op、
   空内容成行、1MB 超长内容、tag 前缀语义、write 打不开文件保留缓冲恢复补写、
   析构自动 write、init 不可创建目录 false 不抛、init 目录为普通文件 false

映射口径（语言机制差异，非功能缺口）：
- main CoutCapture(rdbuf) -> contextlib.redirect_stdout(io.StringIO)；
  _yomk_err_pos_log 直写 stdout 可被捕获，CHECK 断言在捕获作用域外
- main 析构作用域结束触发 -> CPython 引用计数：holder 容器持唯一引用 + del +
  gc.collect() 显式触发 __del__（无引用环即刻归零，语义等价 main 确定性析构）
- main ELogLevel:int + static_cast(99) -> python 直接传 int 99（无 UB 顾虑）

风格：纯 main() + 失败计数，返回非 0 表示存在失败用例（零第三方依赖）
"""

import contextlib
import gc
import io
import os
import re
import shutil
import sys
import tempfile

from YomkServerPy import ConsoleLogger, FileLogger, Log

g_failed = 0
TIME_RE = re.compile(r"\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\]")


def check(cond, msg):
    global g_failed
    line = sys._getframe(1).f_lineno
    if cond:
        print(f"[ OK ] [line {line}] {msg}")
    else:
        print(f"[FAIL] [line {line}] {msg}")
        g_failed += 1


def capture(func):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        func()
    return buf.getvalue()


def check_time_format(content):
    # 每条日志行行首时间戳 [YYYY-MM-DD HH:MM:SS.mmm]（对齐 main checkTimeFormat）
    for line in content.splitlines():
        if "[Debug]" in line or "[Info ]" in line or "[Warn ]" in line or "[Error]" in line:
            if not TIME_RE.match(line):
                return False
    return True


def read_file(path):
    with open(path) as f:
        return f.read()


def main():
    tmp_dir = os.path.join(tempfile.gettempdir(),
                           "yomk_logger_direct_lg1_" + str(os.getpid()))
    os.makedirs(tmp_dir, exist_ok=True)
    check(os.path.isdir(tmp_dir), f"临时目录创建成功: {tmp_dir}")

    # ============ ConsoleLogger 直连 ============
    logger = ConsoleLogger()
    check(logger.get_name() == "MainLogger", "ConsoleLogger 默认名 MainLogger")
    logger.set_name("lg1_direct_console")
    check(logger.get_name() == "lg1_direct_console", "setName/getName 往返一致")

    # 四级别输出：级别标签 + [名] + 内容
    out = capture(lambda: (logger.log(Log.eDebug, "", "lg1_dc_debug_marker"),
                           logger.log(Log.eInfo, "", "lg1_dc_info_marker"),
                           logger.log(Log.eWarn, "", "lg1_dc_warn_marker"),
                           logger.log(Log.eError, "", "lg1_dc_error_marker")))
    check("[Debug] [lg1_direct_console] lg1_dc_debug_marker" in out,
          "eDebug 输出行格式（[Debug] [名] 内容）")
    check("[Info ] [lg1_direct_console] lg1_dc_info_marker" in out, "eInfo 输出行格式")
    check("[Warn ] [lg1_direct_console] lg1_dc_warn_marker" in out, "eWarn 输出行格式")
    check("[Error] [lg1_direct_console] lg1_dc_error_marker" in out, "eError 输出行格式")
    check(check_time_format(out), "控制台行首时间戳格式 [YYYY-MM-DD HH:MM:SS.mmm]")

    # 非法级别 default 降级：YOMK_ERR_POS_LOG 直写 cout 可被捕获（CHECK 在捕获外）
    out = capture(lambda: logger.log(99, "", "lg1_dc_badlevel_marker"))
    check("Unknown log level: 99" in out,
          "非法级别 99 走 default 错误提示（P2-c 修复后活分支）")
    check("[Info ] [lg1_direct_console] lg1_dc_badlevel_marker" in out,
          "非法级别降级 Info 输出 marker")

    # 超长名（4096）与空内容
    long_logger = ConsoleLogger()
    long_name = "L" * 4096
    long_logger.set_name(long_name)
    check(long_logger.get_name() == long_name, "4096 字符超长名 setName/getName 一致")
    out = capture(lambda: long_logger.log(Log.eInfo, "", ""))
    check(("[Info ] [" + long_name + "] ") in out, "超长名 + 空内容日志正常输出")

    # tag 显示语义：tag 非空显示 [tag]，空 tag 回退 [日志器名]
    out = capture(lambda: (logger.log(Log.eInfo, "lg1_dc_tag", "lg1_dc_tagged_marker"),
                           logger.log(Log.eInfo, "", "lg1_dc_fallback_marker")))
    check("[Info ] [lg1_dc_tag] lg1_dc_tagged_marker" in out,
          "tag 非空显示 [tag]（m_tag 优先于日志器名）")
    check("[Info ] [lg1_direct_console] lg1_dc_fallback_marker" in out,
          "空 tag 回退显示 [日志器名]")

    # ============ FileLogger 直连 ============
    fl = FileLogger()
    check(fl.get_name() == "MainLogger", "FileLogger 默认名 MainLogger")
    fl.set_name("lg1_direct_file")
    fl.set_dir(os.path.join(tmp_dir, "fl_basic"))
    check(fl.get_name() == "lg1_direct_file" and
          fl.get_dir() == os.path.join(tmp_dir, "fl_basic"),
          "setName/setDir/getDir 往返一致")

    # init：多级目录 + 空日志文件，返回 True（P2-b 修复后 bool 语义）
    check(fl.init() is True, "init 成功返回 True（P2-b 修复后 bool 语义）")
    log_file = os.path.join(tmp_dir, "fl_basic", "lg1_direct_file.log")
    check(os.path.isdir(os.path.join(tmp_dir, "fl_basic")) and
          os.path.isfile(log_file) and os.path.getsize(log_file) == 0,
          "init 建多级目录与空日志文件")

    # log 四级别：入缓冲不落盘
    fl.log(Log.eDebug, "", "lg1_df_debug_marker")
    fl.log(Log.eInfo, "", "lg1_df_info_marker")
    fl.log(Log.eWarn, "", "lg1_df_warn_marker")
    fl.log(Log.eError, "", "lg1_df_error_marker")
    # 非法级别 default 降级 Info 入缓冲（CHECK 在捕获作用域外）
    out_bad = capture(lambda: fl.log(99, "", "lg1_df_badlevel_marker"))
    check("Unknown log level: 99" in out_bad,
          "FileLogger 非法级别 99 走 default 错误提示（P2-c 修复后活分支）")
    check(os.path.getsize(log_file) == 0, "write 前缓冲内容不落盘（文件仍空）")

    # write：5 行落盘（四级别各一 + 非法级别降级 Info 一行）
    fl.write()
    content = read_file(log_file)
    check("[Debug] lg1_df_debug_marker" in content and
          "[Info ] lg1_df_info_marker" in content and
          "[Warn ] lg1_df_warn_marker" in content and
          "[Error] lg1_df_error_marker" in content,
          "四级别行格式（时间戳+[级别]+内容）落盘")
    check("[Info ] lg1_df_badlevel_marker" in content,
          "非法级别降级 Info 行落盘（P2-c 回填）")
    check(check_time_format(content), "文件日志行首时间戳格式")

    # 缓冲已清空：二次 write 文件不增长
    size_after = os.path.getsize(log_file)
    fl.write()
    check(os.path.getsize(log_file) == size_after,
          "二次 write 无重复内容（缓冲已清空）")

    # 空内容 log 仍成行（追加 1 行）
    fl.log(Log.eInfo, "", "")
    fl.write()
    content2 = read_file(log_file)
    check(len(content2) > size_after and "[Info ] " in content2[size_after:],
          "空内容日志追加成行（[Info ] 空内容）")

    # 1MB 超长内容往返
    big = "B" * (1024 * 1024)
    fl.log(Log.eInfo, "", big)
    fl.write()
    content3 = read_file(log_file)
    check(big in content3, "1MB 超长内容完整落盘")

    # tag 前缀语义：tag 非空行内追加 [tag] 段，空 tag 不加前缀
    fl.log(Log.eInfo, "lg1_df_tag", "lg1_df_tagged_marker")
    fl.log(Log.eInfo, "", "lg1_df_untagged_marker")
    fl.write()
    content_tag = read_file(log_file)
    check("[Info ] [lg1_df_tag] lg1_df_tagged_marker" in content_tag,
          "tag 非空行格式（[级别] [tag] 内容）")
    check("[Info ] lg1_df_untagged_marker" in content_tag, "空 tag 不加 [tag] 前缀")

    # write 打不开文件分支：dir 指向普通文件路径 -> 错误提示 + 缓冲保留不丢
    regular_file = os.path.join(tmp_dir, "fl_regular")
    with open(regular_file, "w") as f:
        f.write("x")
    fail_fl = FileLogger()
    fail_fl.set_name("lg1_writefail")
    fail_fl.set_dir(regular_file)   # <普通文件>/lg1_writefail.log 无法打开
    fail_fl.log(Log.eInfo, "", "lg1_df_writefail_marker")
    out_fail = capture(fail_fl.write)
    check("open log file failed: " + regular_file + "/lg1_writefail.log" in out_fail,
          "write 打不开文件走错误提示分支")
    # 缓冲保留：恢复有效目录后补写成功（内容不丢）
    recover_dir = os.path.join(tmp_dir, "fl_recover")
    os.makedirs(recover_dir, exist_ok=True)
    fail_fl.set_dir(recover_dir)
    fail_fl.write()
    content4 = read_file(os.path.join(recover_dir, "lg1_writefail.log"))
    check("lg1_df_writefail_marker" in content4,
          "write 失败后缓冲保留，恢复目录补写内容不丢")

    # 析构自动 write：缓冲内容落盘（holder 唯一引用 + del + gc 显式触发）
    dtor_dir = os.path.join(tmp_dir, "fl_dtor")
    os.makedirs(dtor_dir, exist_ok=True)
    holder = []
    dtor_fl = FileLogger()
    dtor_fl.set_name("lg1_dtor")
    dtor_fl.set_dir(dtor_dir)
    dtor_fl.init()
    dtor_fl.log(Log.eInfo, "", "lg1_df_dtor_marker")
    holder.append(dtor_fl)
    del dtor_fl           # 不显式 write，释放最后引用触发 __del__
    holder.clear()        # holder 同步释放，至此最后引用归零
    gc.collect()
    content5 = read_file(os.path.join(dtor_dir, "lg1_dtor.log"))
    check("lg1_df_dtor_marker" in content5, "析构自动 write，缓冲内容落盘")

    # P2-b 处置验证：init 对不可创建目录返回 False 不抛异常
    # /proc 下嵌套新目录：非 root 必失败（EACCES/EPERM），init 内部捕获转 False + 错误提示
    bad_fl = FileLogger()
    bad_fl.set_name("lg1_badinit")
    bad_fl.set_dir("/proc/lg1_impossible_dir/sub")
    init_ret_holder = []
    out = capture(lambda: init_ret_holder.append(bad_fl.init()))
    check(init_ret_holder[0] is False,
          "init 不可创建目录返回 False（P2-b 已处置，异常不穿透）")
    check("init file logger fs error" in out,
          "init 失败错误提示含 fs error 信息")

    # P2-b 第二条失败路径：dir 为普通文件（exists 为 True 不走 makedirs，
    # open <普通文件>/<名>.log 必失败 ENOTDIR）-> False + create log file failed
    regular_dir = os.path.join(tmp_dir, "fl_init_regular")
    with open(regular_dir, "w") as f:
        f.write("x")
    reg_fl = FileLogger()
    reg_fl.set_name("lg1_initfail")
    reg_fl.set_dir(regular_dir)
    init_ret_holder = []
    out = capture(lambda: init_ret_holder.append(reg_fl.init()))
    check(init_ret_holder[0] is False,
          "init 目录为普通文件时返回 False（open 打开失败分支）")
    check("create log file failed: " + regular_dir + "/lg1_initfail.log" in out,
          "init 打开失败错误提示含完整日志文件路径")

    # ============ 收尾清理 ============
    shutil.rmtree(tmp_dir, ignore_errors=True)

    if g_failed == 0:
        print("TestYomkLoggerDirect all check passed.")
        return 0
    print(f"TestYomkLoggerDirect FAILED ({g_failed} checks failed).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
