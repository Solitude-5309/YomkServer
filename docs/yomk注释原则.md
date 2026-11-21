# yomk 注释原则（Python）

> 定位：内容参考 C++ 版 yomk 注释原则，形式按 Python 主流写法（PEP 257 + Google 风格），不绑定任何文档生成工具。
> 核心：**注释只写代码表达不了的东西；公开面讲能力与约束，内部只讲前置条件、不变量、取舍与坑。**

## 1. 公开面与内部面

| 面 | 范围 | 写法 |
|---|---|---|
| 公开面 | 域入口 `builder.py`、`YomkServer` / `YomkService` / `YomkBoot`、`msgs/**` 消息类、`url/**` 常量域 | 双语 docstring（英文摘要 + 中文摘要 + 按需分节） |
| 内部面 | 各域服务实现、`EventLoop` / `ContextGroup` / `ConsoleLogger` 等内部类、模块内 helper | 单语中文 `#` 注释 |

判断标准只有一句：**使用者会直接调用的 → 公开面；只在框架内部跑的 → 内部面。**

## 2. 公开面 docstring

- 结构：英文摘要（陈述句 + 句号）→ 空行 → 中文摘要 → 按需分节。
- 分节英文在前、中文紧随同一行：`key: Lookup key. 查询键名。`
- 分节按需，不堆砌：
  - `Args`：仅当参数含义务非自解释（如 `discard` 的取值语义）时写。
  - `Returns`：仅当有返回值时写；返回裸 `None` 不写。
  - `Raises`：仅当本函数主动抛出异常时写。
  - `Note` / `Warning`：不变量、线程安全、所有权、禁止用法。
  - `Usage`：仅 builder 类用于列链式形态，每条一行。
- 类 docstring 写用途 + 一条关键约束；`__init__` 不单独写（由类文档覆盖），除非带所有权或副作用语义。
- 不在 docstring 里写长示例：示例归 `docs/*.md`，此处最多给一条最小链。
- 不写 C++ 类型/函数名、文件名、行号与评审编号；Python 与 C++ 的差异按事实写（`msg(cls)`、`async_()`、`install_func` 的元数据参数）。

```python
def get(self):
    """Read a context value by key and fall back to the default on miss.

    按 key 读取上下文值，未命中时回退默认值。

    Args:
        key: Lookup key. 查询键名。
        default: Fallback value. 未命中时的兜底值。

    Returns:
        Value snapshot. 值快照。

    Warning:
        Do not mutate the snapshot in place. 勿原地修改快照。
    """
```

## 3. 内部面注释

只写这些：

- 锁序、时序等并发不变量（谁在锁内、谁在锁外、升级路径）。
- 前置条件与判活/幂等语义（服务是否存活、重复调用是否安全）。
- 所有权与快照语义（引用是否可跨锁使用、缓冲何时落盘）。
- 性能取舍（为何不在锁内做 I/O、为何整对象替换）。
- workaround 与其成因（Cython 注解边界、帧链不可靠等）。
- 已知限制（不支持的能力与替代路径）。

不写这些：

- 代码动作复述（"遍历列表""加锁查表""返回成功"）。
- 流程旁白与逐行翻译。
- 逐方法模板化注释：六级日志方法、`key/group/value/tag` 等累积方法不逐个写，事实由类头一次性说明。
- 长设计说明：上移模块头或类头 1-3 行，方法内只留一句。

文件头只在存在跨文件设计事实（依赖方向、机制归属、锁序约定）时写；否则不写。长 builder 可用 `# —— 分节名 ——` 分段，非强制。

## 4. 删除清单

- 评审编号与来源标注：`U-xx`、`C-x`、`决策 N`、`闭环 N`、`对齐 C++`、commit hash、`*.h:行号` / `*.cpp:行号`。
- 同一事实在模块头、类文档、方法内三处重复的，只留最贴近的一处（通常是类文档）。
- 与 `docs/*.md` 重复的长场景描述。
- `.pyi` 中的说明性注释：类型桩只留类型。

## 5. 正反例

```python
# 反例：复述动作 + 来源标注 + 重复三处的同一事实
# 双检锁：读锁 miss 后升级写锁二次查找（C-3 已采纳，对齐 C++ YomkLogger.cpp:88）
with self.m_consoleLoggersMutex.gen_rlock():
    ...

# 正例：只讲不变量
# 双检锁：读锁 miss 后升写锁二次查找，logger 引用拷出锁外使用
with self.m_consoleLoggersMutex.gen_rlock():
    ...
```

语言与标点：中文用全角标点，英文术语与标识符保留原文，标识符在正文里用反引号包裹。
