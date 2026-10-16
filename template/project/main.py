import YomkApi
from boot.MyBoot import MyBoot

YomkApi.boot(MyBoot(["/ConfigService"]))
print("main: running, press Enter to exit.")
input()
