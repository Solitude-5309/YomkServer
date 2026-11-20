#pragma once
#include <string_view>

// /YomkServerInfo/* 内置服务 URL 常量:inline constexpr std::string_view 编译期常量,
// 请求链全程 string_view 透传,调用路径零堆分配.
// yomk::url 为开放命名空间:用户自定义服务的 url 常量可按同风格直接追加.
namespace yomk::url
{
// /YomkServerInfo/*
inline constexpr std::string_view srvServices = "/YomkServerInfo/services";
inline constexpr std::string_view srvFunctions = "/YomkServerInfo/functions";
inline constexpr std::string_view srvFunction = "/YomkServerInfo/function";
inline constexpr std::string_view srvAll = "/YomkServerInfo/all";

// 语义描述常量：与上方 URL 常量按序一一对应（installFunc 第四参引用；格式惯例见 docs/YomkServer.md「内省描述惯例」）
inline constexpr const char* srvServicesDesc = "Semantic: 返回全部服务名列表（含框架内置服务）；Returns: StringArray 包";
inline constexpr const char* srvFunctionsDesc = "Semantic: 查询指定服务的函数元信息清单；Returns: FuncInfoList 包；Invalid=服务名空；服务不存在时 Succeeded+空表（现状契约）";
inline constexpr const char* srvFunctionDesc = "Semantic: 按函数 URL 查询单函数元信息（命中时 m_msg 恒等于声明的期望消息类型名）；Returns: FuncInfo 包；Invalid=URL 空/无 / 前缀/无第二段；未命中返回空";
inline constexpr const char* srvAllDesc = "Semantic: 全拓扑快照（服务→函数元信息）；Returns: ServerServiceInfoList 包（m_funcs map 字典序）";
}  // namespace yomk::url