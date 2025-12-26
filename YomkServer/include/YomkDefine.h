#pragma once
#include <memory>
#include <map>
#include <functional>
#include "YomkPkg.h"

#if defined(YOMKSERVER_LIBRARY)
#  define YOMKSERVER_EXPORT __declspec(dllexport)
#else
#  define YOMKSERVER_EXPORT __declspec(dllimport)
#endif

#define YomkInstallFunc(FuncName, Func) installFunc(FuncName, std::bind(&Func, this, std::placeholders::_1))

#define YomkUnPackPkg(pkg, pkgName, ClassName, ptrName) \
    std::shared_ptr<ClassName> ptrName = nullptr; \
    if (pkg && pkg->name() == pkgName) { ptrName = std::dynamic_pointer_cast<ClassName>(pkg); } 

#define YomkUnPackPkgClone(pkg, pkgName, ClassName, ptrName) \
    std::shared_ptr<ClassName> ptrName = nullptr; \
    if (pkg && pkg->name() == pkgName) { ptrName = std::dynamic_pointer_cast<ClassName>(pkg->clone()); } 

#define YomkUnPackPkgRespond(pkg, pkgName, ClassName, ptrName) \
    if (!pkg || pkg->name() != pkgName) return { YomkRespond::eErr, " pkg is null or pkg is not "#pkgName". " }; \
    std::shared_ptr<ClassName> ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    if(!ptrName) return { YomkRespond::eErr, " pkg["#pkgName"] is dynamic_pointer_cast failed. " };

#define YomkUnPackPkgCloneRespond(pkg, pkgName, ClassName, ptrName) \
    if (!pkg || pkg->name() != pkgName) return { YomkRespond::eErr, " pkg is null or pkg is not "#pkgName". " }; \
    std::shared_ptr<ClassName> ptrName = std::dynamic_pointer_cast<ClassName>(pkg->clone()); \
    if(!ptrName) return { YomkRespond::eErr, " pkg["#pkgName"] is dynamic_pointer_cast failed. " };

#define YomkUnPackPkgVoid(pkg, pkgName, ClassName, ptrName) \
    if (!pkg || pkg->name() != pkgName) return ; \
    std::shared_ptr<ClassName> ptrName = std::dynamic_pointer_cast<ClassName>(pkg); \
    if(!ptrName) return ;

#define YomkUnPackPkgCloneVoid(pkg, pkgName, ClassName, ptrName) \
    if (!pkg || pkg->name() != pkgName) return ; \
    std::shared_ptr<ClassName> ptrName = std::dynamic_pointer_cast<ClassName>(pkg->clone()); \
    if(!ptrName) return ;