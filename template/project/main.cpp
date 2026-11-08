#include <YomkServer/YomkServerApi.h>
#include <YomkServer/YomkLoggerApi.h>

#include "boot/MyBoot.h"

using namespace yomk::payload;

int main(int argc, char* argv[])
{
    yomk::server().boot_ptr(new MyBoot(argc, argv, {"/ConfigService"})).run();
    yomk::log().info("running, press Enter to exit.");
    getchar();
    return 0;
}
