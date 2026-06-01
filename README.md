# 智能环境控制系统（env_control）

基于 `LVGL + Linux framebuffer` 的嵌入式环境控制项目，目标分辨率 `1024x600`。  
系统主线是“采集 -> 决策 -> 执行 -> 反馈”。

## 1. 项目里有什么代码

### 1.1 入口与调度代码
- `src/main.c`
  - 负责程序入口。
  - 调用 `app_hardware_init()` 初始化硬件侧。
  - 调用 `app_lvgl_port_init()` 初始化显示与输入。
  - 调用 `setup_ui()/events_init()/custom_init()` 启动 UI 与业务。
  - 主循环执行 `lv_tick_inc()` + `lv_timer_handler()`。

### 1.2 业务核心代码
- `gui/custom.c`（核心）
  - 页面构建：总览页、详情页、模式管理页。
  - 系统状态：`app_state_t`（温湿光、风扇、RGB、模式、历史）。
  - 模式策略：`MODE_MANUAL/AUTO/ECO/NIGHT/DEMO`。
  - 采样与联动：`data_step()`、`auto_update_fan_by_temp()`、`auto_update_lamp_by_env()`。
  - 硬件落地：`apply_hw()`。
  - 历史与日志：内存环形记录 `g_db` + `/tmp/env_sensor_log.csv`。
- `gui/custom.h`
  - 对外接口：`app_hardware_init`、`app_lvgl_port_init`、`app_lvgl_port_deinit`、`app_get_ms`、`custom_build_screen`、`custom_init`。

### 1.3 GUI 生成层代码（SquareLine/LVGL）
- `gui/gui_guider.c/.h`：UI 根结构与加载流程。
- `gui/setup_scr_screen.c`：页面创建入口，调用 `custom_build_screen()`。
- `gui/events_init.c/.h`：事件初始化骨架。
- `gui/widgets_init.c/.h`：通用控件初始化辅助。
- `gui/images/*`：图标资源（PNG 转 C 数组）。
- `gui/guider_fonts/*`、`gui/guider_customer_fonts/*`：字体资源。

### 1.4 设备执行层代码
- `include/board.h`
  - 板级常量：`FAN_GPIO`、`LAMP_R/G/B_GPIO`、`LAMP_ON/OFF_LEVEL`。
- `include/gpio.h` + `device/gpio.c`
  - GPIO 导出、方向设置、读写、反导出。
- `include/fan.h` + `device/fan.c`
  - 风扇控制接口。
  - 兼容 `fan0/fan1` 通道自动探测与写入重试。
- `include/led.h` + `device/led.c`
  - LED 兼容控制接口（基于 GPIO）。

### 1.5 传感器采集层代码
- `sensor/i2c_bus.h/.c`
  - I2C 总线打开/关闭、读写、写后读。
- `sensor/aht20.h/.c`
  - 温湿度初始化与读取，包含 CRC 校验分支。
- `sensor/bh1750.h/.c`
  - 光照初始化与读取。

### 1.6 启动与部署代码
- `boot/bootloader.sh`：拉起主程序，异常退出后重启。
- `boot/env_control.service`：systemd 服务定义。
- `boot/install_autostart.sh`：安装开机自启动。

### 1.7 第三方依赖代码
- `lvgl/`：LVGL 图形库源码。
- `lv_drivers/`：显示与输入驱动源码（本项目主要使用 framebuffer）。

## 2. 具体功能是什么

### 2.1 环境感知
- 读取 AHT20：温度/湿度。
- 读取 BH1750：光照。
- 采样周期：`1000ms`（见 `SENSOR_SAMPLE_PERIOD_MS`）。

### 2.2 设备控制
- 风扇：开/关控制，支持 `fan0/fan1` 自动兼容。
- RGB 灯：R/G/B 三通道独立控制。

### 2.3 五种运行模式
- 手动模式：按钮直接控制风扇和 RGB，自动策略暂停。
- 自动模式：按温湿光阈值联动风扇和灯。
- 节能模式：触发更保守，减少风扇和灯的介入。
- 夜间模式：风扇触发更晚、灯光更低打扰。
- 均衡模式：综合温湿光做折中策略。

### 2.4 页面与交互
- 总览页：实时值、状态摘要、风扇控制、RGB 快捷控制。
- 详情页：分模块详情、历史窗口、阈值编辑与应用。
- 模式页：模式切换与效果说明。

### 2.5 历史与追踪
- 短历史：`HIST_N=12` 的内存窗口用于快速趋势显示。
- 分钟级历史：`g_db` 环形缓冲，最大 `DB_MAX_RECORDS=1440`。
- 落盘日志：`/tmp/env_sensor_log.csv`，便于重启后回看。

## 3. 运行主流程（从上到下）

1. `main()` 调用 `app_hardware_init()`，初始化 GPIO、风扇、I2C、传感器和日志。  
2. 调用 `lv_init()` 和 `app_lvgl_port_init()`，准备 framebuffer 与触控输入。  
3. 调用 `setup_ui()`、`events_init()`、`custom_init()`，建立页面并启动定时器。  
4. `tmr_data()` 每秒触发：`data_step()` 采样 -> 自动策略 -> `apply_hw()` 执行 -> `refresh_all()` 刷新页面。  
5. 主循环持续调用 `lv_timer_handler()`，驱动 UI 与业务计时。

## 4. 工程目录结构（自上到下）

```text
env_control/
├─ src/
│  └─ main.c
├─ gui/
│  ├─ custom.c
│  ├─ custom.h
│  ├─ gui_guider.c
│  ├─ gui_guider.h
│  ├─ setup_scr_screen.c
│  ├─ events_init.c
│  ├─ events_init.h
│  ├─ widgets_init.c
│  ├─ widgets_init.h
│  ├─ images/
│  ├─ guider_fonts/
│  └─ guider_customer_fonts/
├─ sensor/
│  ├─ i2c_bus.c
│  ├─ i2c_bus.h
│  ├─ aht20.c
│  ├─ aht20.h
│  ├─ bh1750.c
│  └─ bh1750.h
├─ device/
│  ├─ gpio.c
│  ├─ fan.c
│  └─ led.c
├─ include/
│  ├─ board.h
│  ├─ gpio.h
│  ├─ fan.h
│  └─ led.h
├─ boot/
│  ├─ bootloader.sh
│  ├─ env_control.service
│  └─ install_autostart.sh
├─ lvgl/                 # 第三方库
├─ lv_drivers/           # 第三方驱动
├─ docs/
├─ build/
├─ Makefile
├─ lv_conf.h
├─ lv_drv_conf.h
└─ README.md
```

## 5. 构建与运行

### 5.1 本地构建
```bash
make clean
make -j4
```

### 5.2 设备端运行
```bash
sudo systemctl stop env_control
sudo ./build/env_control
```

### 5.3 开机自启动安装
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
