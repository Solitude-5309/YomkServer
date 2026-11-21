# yomk 命名原则
> 版本：v2.0
> 性质：**风格指引**——供 yomk 产品族编码与评审参照的默认约定，不是硬性准入；
> 有充分理由可以偏离，说明即可。不做机械检查，最终判据是可读性与一致性。
> 范围：yomk 产品族自身代码的 include/src/docs 命名；不约束客户、使用方与下游。
> 锚定主流规范：Google C++ 风格指南、LLVM 编码规范、gRPC/API 命名惯例。
> 核心思想：**命名即契约——看到名字就能推断它所在的层、它的动作语义，以及它与同端点其他层的对应关系。**

## 1. C++ 基本命名（与主流 C++ 风格一致）
1. 类型/类 PascalCase：`YomkPlugSysBuilder`；函数与成员函数 camelCase：`installFunc()`。
2. 链式 DSL 方法用 snake_case，与普通接口 camelCase 形成层感区分：
   `plug_sys().plg_name(n).get_info()`。判据是"是否在链式表达式内"，不是"是否用户可见"。
3. 载荷 struct 字段 camelCase：`funcName`、`manifestPath`。
4. 成员变量 `m_` 前缀；**所有非宏常量一律 `k` 前缀 + PascalCase**——编译期数值、字符串常量、
   URL 常量、语义描述常量、静态数据成员同此一条规则：`m_msgName`、`kName`、`kDigitBufSize`、
   `kCtxCreate`、`kCtxCreateDesc`。判据是"是否声明为 `const`/`constexpr` 的具名常量"，
   与它是否用户可见、是否为 `string_view` 无关；枚举值（`ResStatus::Succeeded`）是枚举类型的
   成员，不套 `k` 前缀。
5. 宏与版本宏全大写下划线、**逐词分写**：`YOMK_DEFINE_MSG`、`YOMK_PLUGIN_ABI_VERSION`
   （❌ `YOMKPLUGIN_ABI_VERSION` 式连写）。宏与常量风格不混用：一旦写成 `constexpr` 声明，
   就不得再取 `YOMK_*` 全大写名（例：诊断级别 `kYomkDiagLevelWarn`，不是 `YOMK_DIAG_LEVEL_WARN`；
   两者在作用域、类型与调试体验上都不是一回事）。

## 2. 端点命名：动词优先
1. 传输为单一 request() 的端点即 RPC 方法名，查询/枚举类动词置首：
   - `get_`：点查单一对象 → `/get_topic_info`
   - `list_`：枚举清单 → `/list_topics`
   - `all`：结构化全量导出 → `/all`
2. 控制动词成对对称：`enable/disable`、`create/destroy`、`load/unload`。
3. 服务前缀已表达域，路径内不再自我前缀：✅ `/YomkFunctionPool/register`，
   ❌ `/YomkLogger/write_file_log`（应为 `/flush`）。
4. 端点各层保持同词对应：URL `get_<宾语>` ↔ 客户端点查、`list_<宾语>s` ↔ 枚举；
   改动时 URL 路径、URL 常量、客户端接口、消息、handler 各层一次改齐。

## 3. 一词一义
1. 同一概念在体系内一个词、跨层一致：消息指针统一 `msg`（❌ pkg/data/value 混用）。
2. 名称须指明用途，不取泛化词：❌ `Function`（应为 `FuncRegistration`，与 `CallFunction` 对仗）。
3. 消息族后缀按用途习惯化：`*Info`（内省快照）、`*Query`（单查载荷）、`*List`（清单）、
   `*Req`（请求载荷）。
4. 文档/注释/错误文案与代码同词：术语漂移即命名漂移。

## 4. 缩写克制
1. 全拼优先，不新造晦涩缩写（❌ `fp`、`mgr` 式）。
2. 通用缩写 `id`/`url`/`msg` 可用；既有域入口缩写（`ctx`、`fn_pool`、`log_mgr` 等）
   定版保留、不追溯；新代码不再新增缩写。

## 5. 前缀与域词（产品自定）
1. 域前缀取各产品的开发习惯，产品自定并在产品内保持一致；
   本文档不做登记与准入，也不约束产品外的使用方。
2. 建议取服务名去产品名的驼峰：服务 `/YomkFunctionPool` → 域前缀 `FnPool`（或既定缩写）。
3. **常量名 = `k` + 域前缀驼峰 + 端点驼峰**，与路径逐字对齐（§1.4 的 `k` 规则对 URL 常量同样生效）：
   `kDdsGetVersion = "/YomkDdsHandler/get_version"`、`kCtxCreate = "/YomkContext/create"`；
   语义描述常量取同名 + `Desc`：`kCtxCreateDesc`。
4. 现有产品域前缀速查（仅记录，非准入）：YomkServer `Ctx`/`EventLoop`/`FnPool`/`Logger`/`Srv`、
   YomkDds `Dds`、YomkPlugSys `Plugin`（即 `kCtxCreate`、`kEventLoopStart`、`kFnPoolRegister`、
   `kLoggerConsoleLog`、`kSrvServices`）。

## 6. 目录与文件
1. 目录取域入口短名，文件取类全名：`include/yomk/ctx/YomkContextApi.h`；
   模块聚合头以 `Api` 结尾（`YomkPlugSysApi.h`）。

## 7. 现有惯例速查（仅记录）
1. 内省三件套：点查 `get_info()`、枚举 `list_<宾语>()`、全量 `all()`。
2. 分面/链式类后缀 `Builder`；三态结果枚举 `{Invalid, Succeeded, Failed}`。
3. 消息包成员名 `d`、运行时类型标识 `kName`、消息命名空间 `yomk::msgs`、载荷 `yomk::payload`。
4. 常量命名：`k` + 域前缀驼峰 + 端点驼峰；语义描述常量为同名 + `Desc`
   （`kCtxCreate` / `kCtxCreateDesc`，URL 常量 `string_view`、描述常量 `const char*`）。

## 结语
> 风格一致比风格选择更重要：选定一种，贯穿始终。本指引在评审中参照执行，
> 偏离时以"可读性与一致性"为最终判据。
