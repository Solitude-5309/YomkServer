#include "YomkAPI.h"

// 第十三轮：单例锁与持有器已改为永生存储（见 YomkAPI.h mtxStorage/holderStorage），
// 不再需要文件作用域静态成员定义，退出清理改由 init() 内 atexit 注册

std::string YomkAPI::version()
{
    return YOMKSERVER_VERSION;
}