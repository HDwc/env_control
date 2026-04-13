# Env Control 自启动说明

## 文件
- `boot/bootloader.sh`：启动主程序，异常退出后自动重启
- `boot/env_control.service`：systemd 开机自启动配置
- `boot/install_autostart.sh`：一键安装并启用自启动
- `GUI_START_DELAY`：启动前延迟秒数（默认 3）
- `TIME_SYNC_WAIT_SEC`：等待 NTP 同步的最长秒数（默认 20，超时继续启动）

## 一次性安装（在目标 Linux 设备上执行）
```bash
cd /home/hd/env_control
chmod +x boot/bootloader.sh boot/install_autostart.sh
./boot/install_autostart.sh
```

## 验证
```bash
systemctl is-enabled env_control.service
systemctl status env_control.service --no-pager -l
```

重启设备后，程序会自动启动，不再需要手动执行启动命令。
