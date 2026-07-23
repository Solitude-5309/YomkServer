import YomkApi
from pathlib import Path
import sys
sys.path.append(Path.cwd())
from boot.MyBoot import MyBoot

YomkApi.boot(MyBoot(["/ConfigService"]))
input("TemplateProject is running, press Enter to exit.")