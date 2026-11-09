# yomk 注释原则
> 版本：v1.2
> 定位：对外公开API 对齐 ROS2 / FastDDS / CycloneDDS 文档风格；内部实现对齐 STL 极简思想，仅保留关键注释。
> 核心思想：**对外注释面向使用者，讲清能力与约束；内部注释面向维护者，只写代码无法表达的前提、不变量与特殊取舍。**
> 双语规则：英文为权威主注释（Doxygen 文档源）；`// @zh:` 中文为配套翻译，仅用于国内开发者阅读源码，不参与Doxygen文档生成。

## 1. 适用划分
- **规则A（Doxygen 英文文档注释 + 可选 @zh 中文翻译）**：`.hpp` / `.h` 中对外导出、public 接口、Builder 类、消息类型、枚举、模块入口。
- **规则B（STL 极简注释，无双语要求）**：`.cpp` 实现、private 成员、内部 helper、匿名命名空间、内部结构体、实现细节。
> 一句话判断：别人会调用的 API → 规则A；只在框架内部调用、使用者看不到的代码 → 规则B。

## 2. 规则A：对外公开API注释规范（双语模式）
### 2.1 基本原则
1. 英文 Doxygen 块是**唯一权威源**，写给调用方，描述接口能力、约束、风险，**不解释内部实现逻辑**。
2. 英文块使用 Doxygen 块注释 `/** */`，放置在类/函数/枚举上方；**必须先写英文Doxygen块，紧随其后放置 `// @zh:` 中文翻译组**。
3. 禁止复述代码语义，不要写和函数名、参数名重复的废话。
4. 机器人中间件重点信息必须按需标注：线程安全、是否阻塞、内存所有权、回调约束、异常行为、未定义行为。
5. 模板接口、链式 Builder 额外补充类型约束、调用顺序约束。
6. 废弃接口使用 `@deprecated`，标注替代方案。
7. 中文 `@zh:` 仅做翻译，**不新增、不修改、不补充英文不存在的语义**；中文可以缺失（允许只有英文），禁止只有中文而无英文Doxygen主注释。
8. 中英条目顺序一一对应，结构对齐；接口变更优先更新英文，再同步中文；翻译过期不一致时直接删除对应`@zh:`行，禁止保留矛盾注释。

### 2.2 英文Doxygen可用标签（按需选用，禁止堆砌）
- `@brief`：一句话概要（**必选**）
- `@param[in] / @param[out] / @param[in,out]`：参数说明
- `@return`：返回值含义，特殊返回场景（空/失败）说明
- `@note`：重要特性、使用说明
- `@warning`：高危约束、常见踩坑点
- `@pre`：前置条件，不满足将产生未定义行为
- `@tparam`：模板参数约束
- `@deprecated`：废弃接口，推荐新接口

### 2.3 @zh 中文翻译书写规则
1. 每行中文注释以 `// @zh:` 开头，`//` 后保留一个空格；
2. 结构对齐英文标签：一条英文标签对应一条 `// @zh:`；
3. 中文里不需要写 Doxygen 标签（`@brief`/`@param` 等），自然描述即可；
4. 中文标点使用中文标点；英文Doxygen块内固定使用英文标点；
5. 禁止在 `@zh:` 行内混入英文短句、术语穿插；术语建立固定映射表（thread-safe→线程安全、non-blocking→非阻塞、undefined behavior→未定义行为）。

### 2.4 对外示例
```cpp
/**
 * @brief Read configuration value by key
 * @param key Configuration lookup key
 * @param default_val Fallback value if key does not exist
 * @return Stored string value
 * @note Thread-safe, non-blocking
 * @warning Empty key will not match any entry
 */
// @zh: 根据键名读取上下文配置项
// @zh: key 配置查询键名
// @zh: default_val 键不存在时返回的兜底值
// @zh: return 配置字符串
// @zh: note 线程安全，非阻塞
// @zh: warning 传入空key不会报错，但不会匹配任何配置
std::string get(const std::string& key, const std::string& default_val);

/**
 * @brief Context configuration query builder
 * Chain to construct query conditions, call get() to execute the query.
 */
// @zh: 上下文配置查询构建器
// @zh: 链式构造查询条件，调用 get() 终止并执行查询
class ContextQueryBuilder
{
public:
    /**
     * @brief Set target lookup key
     * @param key Target key
     * @return Reference to self for chaining
     */
    // @zh: 指定查询键名
    // @zh: key 目标键
    // @zh: return 自身链式引用
    ContextQueryBuilder& key(std::string key);

    /**
     * @brief Terminate chain and execute query
     * @return Queried string result
     * @note Blocking until config lookup completes
     */
    // @zh: 终止链式调用并执行查询
    // @zh: return 查询得到的字符串结果
    // @zh: note 阻塞直至配置查询完成
    std::string get();
};
```

## 3. 规则B：内部源码注释规范（对齐 STL，**不要求双语**）
### 3.1 基本原则
> 优先依靠命名、类型、常量、结构表达逻辑；**无特殊约束就不写注释**。
> 注释仅用于记录代码无法直观表达的信息。
1. 仅使用单行注释 `// `，`//` 后保留一个空格；**内部代码禁止写 Doxygen 块注释，禁止使用 @zh 双语注释**。
2. 仅在以下场景写注释：
   - 前置条件（Precondition）：调用前必须满足的前提，否则 UB
   - 不变量（invariant）：对象/数据结构始终维持的约束
   - 算法复杂度、性能取舍
   - 反直觉写法、特殊兼容 workaround
   - 已知边界缺陷、临时限制
3. 禁止注释描述循环、分支、赋值等普通执行动作；不写“遍历列表”“累加计数”这类复述代码的注释。
4. 内部 helper、私有函数、局部变量默认不加注释。
5. 注释语言团队自选中文或英文，只保留单版本。

### 3.2 内部示例
```cpp
// Precondition: buf != nullptr, buf->size() < max_capacity
static void append_frame(Buffer* buf, std::string_view payload)
{
    // invariant: write_pos <= capacity
    // 环形覆盖策略，队列满时直接丢弃旧数据，不阻塞生产者
}
```
❌ 禁止内部出现这类注释：
```cpp
// 遍历所有sink，写入日志
for (auto& sink : sinks_) {
    sink->write(msg);
}
```

## 4. 链式API（Builder）专项约定
yomk 大量使用链式 Builder 风格，单独约定：
1. Builder 类本体：规则A，英文Doxygen注释说明整体用途、终止函数、调用约束；配套 `// @zh:` 中文翻译。
2. 链式中间方法（`.key()` `.type()` `.default()`）：简短Doxygen，说明作用 + 返回自身以支持链式调用；配套一行 `// @zh:`。
3. 终止函数（`.get()` / `.emit()` / `.request()`）：重点标注阻塞性、线程安全、返回值、生命周期约束；中文翻译同步对应风险点。
4. Builder 内部状态、私有成员：规则B，仅写不变量与前置条件，**不加 @zh 双语注释**。

## 5. 全局格式统一约定
1. 对外文档注释：`/** */`（英文Doxygen）；中文翻译：`// @zh:`；内部说明注释：`// `；项目内不混用 `///` 文档注释。
2. 英文Doxygen块内标点统一使用英文标点；`@zh:`中文行使用中文标点。
3. 文档唯一对外来源：头文件 public 接口的英文Doxygen；`.cpp` 实现中不写Doxygen文档、不写@zh翻译。
4. 注释同步原则：代码变更后，过时注释优先直接删除，不要保留过期注释；双语注释必须同步校验。
5. 禁止用注释弥补糟糕命名；命名优先于注释。
6. `// @zh:` 是项目自定义标记，**不属于Doxygen标签**，Doxygen生成文档时不会解析这部分内容。

## 6. 红线禁止清单
- ❌ 在 cpp 实现里写 Doxygen 文档注释
- ❌ 内部实现大量堆砌注释
- ❌ 注释内容和代码动作完全重复
- ❌ 保留过期、和代码不一致的注释
- ❌ 依靠注释掩盖模糊、难懂的命名
- ❌ 同一条Doxygen描述句子内部中英穿插
- ❌ 写两套独立 `/** */` Doxygen块（造成文档冲突）
- ❌ 中文`@zh:`翻译新增英文不存在的约束、行为、参数
- ❌ 只写中文`@zh:`，缺失英文Doxygen主注释
- ❌ 规则B内部代码使用 `// @zh:` 双语注释
- ❌ 文件头注释复述用法、与 Doxygen 主注释重复（违反第 8 节）

## 7. 快速检索口诀
> 对外头文件API：英文主注释（ROS2风格，Doxygen输出）；`// @zh:`中文后置翻译，条目一一对应；可缺中文，不可缺英文。
> 内部实现源码：无前置、无不变量、无特殊取舍 → 不写注释（STL风格），不做双语。

## 8. 文件头注释专项约定（File-Header Comment Convention）
> 适用范围：各模块公开头文件（Api/Msg/Payload 等）顶部的 `// ` 域概述注释。
> Scope: the `// ` domain-overview comment block at the top of each module's public header.

1. 文件头注释面向**维护者**，只写模块级设计取舍：依赖方向、内部机制归属、拆分决策、不变量。
   1. File-header comments target **maintainers** and record only module-level design decisions: dependency direction, ownership of internal mechanisms, split decisions, and invariants.
2. 用法说明一律不写入文件头——对外用法的**唯一来源**是英文 Doxygen 主注释（含 `@zh:` 配套）。
   2. Never document usage in the file header — the **sole source** for public usage is the English Doxygen main comment (with its `@zh:` companion).
3. 使用 `// ` 单行注释、单语言版本（与该头文件的字段注释语言一致）；不加 `@zh:`——`@zh:` 仅服务于 Doxygen 块。
   3. Use single-line `// ` comments in a single language (consistent with the field comments of that header); no `@zh:` — it serves Doxygen blocks only.
4. 不使用 `@file` Doxygen 块——维护者视角的设计信息不应进入生成文档。
   4. Do not use an `@file` Doxygen block — maintainer-level design information must not leak into generated documentation.
5. 无设计取舍可写的文件不写文件头注释（用法即全部内容的文件，Doxygen 已覆盖）。
   5. Files with no design decisions worth recording get no file-header comment (when usage is the whole story, the Doxygen already covers it).
6. 禁止文件头注释与 Doxygen 内容重复；接口变更时文件头只同步设计事实，过时设计描述直接删除。
   6. File-header comments must not duplicate Doxygen content; on interface changes, sync only design facts, and delete outdated design statements outright.

---

如果你需要，我可以额外给你一份配套的：
1. 术语中英映射表（中间件专用）
2. 简易python校验脚本：自动检查`@zh:`和英文Doxygen条目是否对齐、有没有孤立@zh注释
3. 给clang-format补充注释相关格式化规则