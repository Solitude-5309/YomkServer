from yomk.server import server
from yomk.log import log
from boot.MyBoot import MyBoot

server().boot_ptr(MyBoot(["/ConfigService"])).run()
log().tag("main").info("running, press Enter to exit.")
input()
