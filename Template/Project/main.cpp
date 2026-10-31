#include <YomkServer/YomkAPI.h>

#include "boot/MyBoot.h"

using namespace yomk;

int main(int argc, char* argv[])
{
    YomkAPI::boot(new MyBoot(argc, argv, {"/ConfigService"}));
    YOMK_INFO_TAG("main", "running, press Enter to exit.");
    getchar();
    return 0;
}
