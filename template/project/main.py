import YomkApi
from boot.MyBoot import MyBoot

YomkApi.boot(MyBoot(["/ConfigService"]))
YomkApi.console_log_info_tag("main", "running, press Enter to exit.")
input()
