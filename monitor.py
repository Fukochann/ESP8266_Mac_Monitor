import psutil
import requests
import time
import sys

# ================= 配置区域 =================
ESP_IP = "192.168.1.2"  # ★★★ 请修改为你屏幕上显示的 IP ★★★
# ===========================================

URL = f"http://{ESP_IP}/api/mac"

print(f"监控已启动 (CPU/RAM/网速) -> 发送至 {ESP_IP}")

def get_size(bytes):
    """ 将字节转换为易读格式 (KB/s, MB/s) """
    for unit in ['', 'K', 'M', 'G']:
        if bytes < 1024:
            return f"{bytes:.1f}{unit}"
        bytes /= 1024
    return f"{bytes:.1f}T"

# 初始化网速基准
last_net = psutil.net_io_counters()
last_time = time.time()

try:
    while True:
        # 1. 获取基础数据
        cpu = int(psutil.cpu_percent(interval=None))
        ram = int(psutil.virtual_memory().percent)
        
        # 2. 计算实时网速
        current_net = psutil.net_io_counters()
        current_time = time.time()
        time_delta = current_time - last_time
        
        if time_delta > 0:
            # 计算每秒字节数
            bytes_sent = (current_net.bytes_sent - last_net.bytes_sent) / time_delta
            bytes_recv = (current_net.bytes_recv - last_net.bytes_recv) / time_delta
            
            up_str = f"{get_size(bytes_sent)}/s"
            down_str = f"{get_size(bytes_recv)}/s"
        else:
            up_str = "0K/s"
            down_str = "0K/s"
            
        # 更新基准
        last_net = current_net
        last_time = current_time

        # 3. 终端打印预览
        sys.stdout.write(f"\rCPU:{cpu}% RAM:{ram}% ↑{up_str} ↓{down_str}    ")
        sys.stdout.flush()

        # 4. 发送数据给 ESP8266
        try:
            params = {
                'cpu': cpu, 
                'ram': ram, 
                'up': up_str, 
                'down': down_str
            }
            requests.get(URL, params=params, timeout=0.5)
        except:
            pass # 忽略网络超时，防止脚本崩溃

        time.sleep(1) # 1秒刷新一次

except KeyboardInterrupt:
    print("\n已停止监控。")