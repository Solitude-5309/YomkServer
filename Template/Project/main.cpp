#include <YomkServer/YomkApi.h>

#include "boot/MyBoot.h"

using namespace yomk::payload;

int main(int argc, char* argv[])
{
    YomkAPI::boot(new MyBoot(argc, argv, {"/ConfigService"}));
    yomk::log().tag("main").info("running, press Enter to exit.");
    getchar();
    return 0;
}
