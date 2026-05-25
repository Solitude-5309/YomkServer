#pragma once
#include <memory>
#include <map>
#include <functional>
#include "YomkPkg.h"

#if defined(_WIN32) || defined(_WIN64)
    // Windows 平台（32/64位）
    #if defined(YOMKSERVER_LIBRARY)
        #define YOMKSERVER_EXPORT __declspec(dllexport)
    #else
        #define YOMKSERVER_EXPORT __declspec(dllimport)
    #endif
#elif defined(__linux__)
    #define YOMKSERVER_EXPORT __attribute__((visibility("default")))
#else
    #define YOMKSERVER_EXPORT __attribute__((visibility("default")))
#endif

#define YomkInstallFunc(FuncName, Func) installFunc(FuncName, std::bind(&Func, this, std::placeholders::_1))

#define YomkUnPackPkgresponse(pkg, pkgName, ClassName, ptrName) \
    if (!pkg || pkg->name() != pkgName) return { YomkResponse::eErr, " pkg is null or pkg is not "#pkgName". " }; \
    std::shared_ptr<ClassName> ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    if(!ptrName) return { YomkResponse::eErr, " pkg["#pkgName"] is dynamic_pointer_cast failed. " };

#define YomkUnPackPkgResponse(pkg, ClassName, ptrName) \
    if (!pkg || pkg->name() != #ClassName) return { YomkResponse::eErr, " pkg is null or pkg is not "#ClassName". " }; \
    YomkPtr(ClassName) ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg); \
    if(!ptrName) return { YomkResponse::eErr, " pkg["#ClassName"] is dynamic_pointer_cast failed. " };

#define YomkUnPackPkgVoid(pkg, ClassName, ptrName) \
    if (!pkg || pkg->name() != #ClassName) return ; \
    YomkPtr(ClassName) ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg); \
    if(!ptrName) return ;

#define YomkUnPackPkg(pkg, ClassName, ptrName) \
    YomkPtr(ClassName) ptrName = nullptr; \
    if (pkg && pkg->name() == #ClassName) { ptrName = std::dynamic_pointer_cast<Yomk(ClassName)>(pkg); } 

#define YomkUnPackPkgT(pkg, pkgName, ClassName, ptrName) \
    std::shared_ptr<ClassName> ptrName = nullptr; \
    if (pkg && pkg->name() == pkgName) { ptrName = std::dynamic_pointer_cast<ClassName>(pkg); } 
