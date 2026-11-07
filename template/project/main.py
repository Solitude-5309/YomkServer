import YomkApi
from boot.MyBoot import MyBoot
from YomkApi import log, server

server().boot_ptr(MyBoot(["/ConfigService"])).run()
log().tag("main").info("running, press Enter to exit.")
input()
