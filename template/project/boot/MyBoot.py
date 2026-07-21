import YomkApi
import sys
from pathlib import Path
current_dir = Path(__file__).resolve()
test_dir = current_dir.parent
sys.path.append(str(test_dir))

from services.YomkServiceA import YomkServiceA

class MyBoot(YomkApi.YomkBoot):
    def __init__(self, srv_names):
        super().__init__()
        self.srv_names = srv_names
    def before(self):
        print("MyBoot before")
        # 服务启动前的初始化操作
        # 服务启动前创建CONTEXT，确保在服务启动时能够访问上下文
        # 服务启动前创建EVENTLOOP，确保在服务启动时能够使用特定的事件循环
        # 服务启动前注册功能函数到FUNCTION_POOL，确保在服务启动时能够访问功能函数
        # 服务启动前创建YOMK_SET_CONSOLE_LOG_PROXY，确保在服务启动时能够触发日志代理
        # 服务启动前创建其他必要的资源，确保在服务启动时能够使用   
        return 0
    def start(self):
        print("MyBoot start")
        
        cur_srvs = {
            "/YomkServiceA": YomkServiceA(YomkApi.server()),
        }
        
        for srv_name in self.srv_names:
            if srv_name in cur_srvs:
                YomkApi.add_service(cur_srvs[srv_name], srv_name)
                
        return 0
    def after(self):
        print("MyBoot after")
        # 服务启动后的善后操作
        # 调用服务接口进行服务内部初始化操作
        # 调用服务接口自启动某个任务
        return 0
