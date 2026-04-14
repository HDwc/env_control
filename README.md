# 智能环境控制系统（env_control）

## 1. 项目定位
本项目是基于 **LVGL + Linux framebuffer** 的嵌入式智能环境控制系统，目标分辨率 **1024x600**。

系统核心能力：
- 环境感知：AHT20（温湿度）、BH1750（光照）
- 执行控制：风扇（fan0/fan1）、RGB 灯（GPIO117/116/109）
- 策略联动：手动/自动/节能/夜间/均衡 5 种模式
- 可视化：总览、详情、模式管理、历史数据
- 记录追溯：内存环形历史 + CSV 日志

## 2. “智能”体现在哪里
1. 感知闭环：固定周期采集真实传感器数据。  
2. 策略决策：按“模式 + 阈值 + 多传感器条件”计算动作。  
3. 联动执行：统一执行到风扇和 RGB 硬件。  
4. 状态反馈：UI + 历史数据可回看策略效果。  

## 3. 模式与效果
### 手动模式
- 人工接管，风扇/RGB 按按钮立即执行。  
- 自动策略暂停。  

### 自动模式
- 标准联动策略。  
- 温度/湿度触发风扇，光照/温湿度触发 RGB。  

### 节能模式
- 功耗优先，触发更保守。  
- 非必要尽量少开风扇，RGB 偏低干预。  

### 夜间模式
- 静音和低打扰优先。  
- 风扇触发更晚，灯光策略更柔和。  

### 均衡模式
- 舒适与功耗折中。  
- 响应不激进也不保守。  

## 4. 模块联动关系
1. 采集层：AHT20/BH1750 获取环境数据。  
2. 状态层：更新全局状态（当前值、阈值、模式、历史）。  
3. 策略层：`auto_update_fan_by_temp()` / `auto_update_lamp_by_env()`。  
4. 执行层：`apply_hw()` 统一落地到风扇和 GPIO。  
5. 展示层：刷新总览/详情/模式页和历史窗口。  

## 5. RGB 灯应用场景
RGB 不是装饰，而是状态编码：
- R：温度偏高提示
- B：湿度偏高提示
- G：照度偏低提示
- 组合色：多条件同时满足

价值：无需看数字即可快速判断环境状态，适合巡检和演示。

## 6. 风扇与 RGB 的控制机制
- 传感器模块只提供数据，不直接控制硬件。  
- 策略模块决定“是否动作”。  
- `apply_hw()` 统一执行。  
- 风扇支持 fan0/fan1 兼容控制（避免换线后失控）。  

## 7. 历史数据规则
- `1分`：最近 10 条，每条间隔 1 分钟  
- `10分`：最近 10 条，每条间隔 10 分钟  
- `1时`：最近 10 条，每条间隔 1 小时  
- 时间显示格式：`MM-DD HH:MM:SS`  
- 温度单位：`°C`  

## 8. 目录与文件功能（核心自研文件）
> 注：`lvgl/`、`lv_drivers/` 为第三方框架源码，不在此逐文件展开。

### 根目录
- `Makefile`：项目编译入口。
- `lv_conf.h`：LVGL 配置。
- `lv_drv_conf.h`：显示/输入驱动配置。
- `README.md`：项目总说明（本文件）。

### src/
- `src/main.c`：程序入口，初始化硬件/LVGL，运行主循环。

### include/
- `include/board.h`：硬件引脚与板级常量。
- `include/fan.h`：风扇控制接口声明。
- `include/gpio.h`：GPIO 抽象接口。
- `include/led.h`：LED 兼容接口声明。

### device/
- `device/fan.c`：风扇控制实现（fan0/fan1 兼容控制、状态读取）。
- `device/gpio.c`：GPIO 导出、方向设置、读写。
- `device/led.c`：LED 基础控制（兼容层）。

### sensor/
- `sensor/i2c_bus.c/.h`：I2C 总线打开/读写封装。
- `sensor/aht20.c/.h`：AHT20 温湿度读取。
- `sensor/bh1750.c/.h`：BH1750 光照读取。

### gui/
- `gui/custom.c`：核心业务文件（页面构建、事件、模式策略、数据刷新、历史日志）。
- `gui/custom.h`：`custom.c` 对外接口。
- `gui/gui_guider.c/.h`：GUI 结构体与页面初始化骨架。
- `gui/setup_scr_screen.c`：主屏创建入口。
- `gui/events_init.c/.h`：GUI 事件初始化。
- `gui/widgets_init.c/.h`：组件初始化（生成代码）。
- `gui/images/*`：图像资源。
- `gui/guider_fonts/*`：字体资源。

### boot/
- `boot/bootloader.sh`：启动主程序并在异常退出后拉起。
- `boot/env_control.service`：systemd 自启动服务配置。
- `boot/install_autostart.sh`：一键安装/启用自启动。

### 其它
- `build/`：编译产物目录。

## 9. 构建与部署
### 本地构建
```bash
make clean
make -j4
```

### 程序运行（设备端）
```bash
sudo systemctl stop env_control
sudo ./build/env_control
```

### 代码传输（Windows -> 设备）
```bash
scp -r E:/bishe/env_control hd@192.168.8.14:/home/hd/
```

## 10. 开机自启动
在目标设备执行：
```bash
cd /home/hd/env_control
chmod +x boot/bootloader.sh boot/install_autostart.sh
./boot/install_autostart.sh
```

验证：
```bash
systemctl is-enabled env_control.service
systemctl status env_control.service --no-pager -l
```

## 11. 验证建议
1. 首页观察温湿度/光照实时变化。  
2. 详情页检查历史数据更新与时间粒度切换。  
3. 模式管理逐个切换，确认风扇/RGB联动效果。  
4. 风扇换线后验证控制仍生效。  
