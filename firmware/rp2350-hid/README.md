# RKMoon RP2350 HID 桥固件

Waveshare RP2350-USB-C 上运行的 USB HID 桥固件，协议与硬件设计见 [ADR-012](../../docs/ADR-012-rp2350-hid-bridge.md)。

> **状态：仅编译验证，未上板。** 板子、USB 枚举、被控机输入、remote wakeup、WS2812 灯色、串口实际收发都没有在真实硬件上测试过。主机端单元测试只覆盖与硬件无关的协议解析、CRC、看门狗和报告队列。

## 功能

- 原生 USB-C：复合 HID 设备，三个接口
  - 键盘：boot 协议，8 字节报告，6 键无冲，接受 Caps/Num Lock 输出报告（v0.1 忽略）。
  - 相对鼠标：5 键，dx/dy int16，垂直滚轮，水平滚轮（AC Pan）。主机选 boot 协议时自动改发 3 字节 boot 报告。
  - 绝对指针：5 键，x/y 0..32767，垂直滚轮，水平滚轮。
  - 配置描述符声明 remote wakeup；被控机挂起时，带按键或按钮按下的输入帧会触发唤醒。
- 控制通道 A：UART0，GP0=TX，GP1=RX，1 000 000 baud，8N1，协议 v1。
- 500 ms 看门狗：有按键或按钮按下、且 500 ms 没有收到活动通道的合法帧时，自动全释放。另有 1 s 芯片看门狗防止主循环卡死。
- 相对移动拆分（超过单个报告范围时分多次发送）与合并（队列将满时累加同按钮状态的移动），按键和按钮变化从不丢弃。
- WS2812 状态灯（GP16）。
- 控制通道 B（PIO-USB CDC）：**未实现，编译开关默认关闭**，原因见下文。

## 刷写

1. 按住板子上的 **BOOT** 键，把板子的原生 USB-C 口插到电脑上，然后松开。
2. 电脑上会出现一个名为 `RP2350` 的 U 盘。
3. 把 `rkmoon_rp2350_hid.uf2` 拖进去。拷贝完成后板子自动重启，U 盘消失。

已经刷过本固件的板子，也可以按住 BOOT 再按一下 RESET（RUN）进入同样的 U 盘模式。

## 接线

- 板子原生 USB-C 口 → 被控电脑。板子由被控电脑供电。
- 控制通道 A：T6 的 USB-A 口插一个 **3.3 V** USB 转 TTL 模块（CH340/CP2102/FT232 均可，必须是 3.3 V 电平），**只接三根线**：

| USB 转 TTL 模块 | RP2350-USB-C |
|---|---|
| TXD | GP1（UART0 RX） |
| RXD | GP0（UART0 TX） |
| GND | GND |

  不要接模块的 5V 或 3V3：板子已由被控电脑供电，两边电源互接会倒灌。TX 和 RX 要交叉。T6 上不需要改设备树。

- 板子上的 PIO-USB Type-C 母口不要连接。它的 VBUS 与原生口的 VBUS 在板上直接相连，用普通 USB 线连另一台主机会让两台主机的 5V 短接。

## 状态灯

| 颜色 | 含义 |
|---|---|
| 红 | USB 未枚举（未配置，或被控机未上电） |
| 绿 | USB 已配置，空闲 |
| 蓝（闪） | 刚收到合法输入帧（每帧点亮约 60 ms，连续输入时常亮蓝） |
| 黄 | 500 ms 看门狗刚触发过全释放（保持 2 秒后回到红/绿） |

亮度刻意调低（约 10%）。被控机挂起时灯仍为绿色；是否挂起看 PONG 的 bit1。

## USB 标识

| 项目 | 默认值 |
|---|---|
| VID | `0x1209`（pid.codes 开源 VID） |
| PID | `0x0001`（pid.codes 的测试 PID，仅限内部测试，不可对外分发） |
| 厂商字符串 | RKMoon |
| 产品字符串 | RKMoon HID Bridge |
| 序列号 | 芯片唯一 ID，16 位大写十六进制（与 INFO 帧里的 8 字节序列号相同） |
| bcdDevice | 0x0100 |

编译时可改：`-DRKMOON_USB_VID=0x.... -DRKMOON_USB_PID=0x....`。改 PID 后 Windows 会把它当成新设备重新装驱动（系统自带 HID 驱动，无需额外安装）。

## 编译

需要 Docker（Mac 上已验证 Docker Desktop，aarch64）。本机不需要装 arm-none-eabi 工具链。

```sh
firmware/rp2350-hid/tools/build.sh
```

脚本会：按 `deps.lock` 拉取并核对依赖 commit（放在 `build/rp2350-hid-deps/`，不入库）；构建 Docker 镜像 `rkmoon-rp2350-build:1`（Ubuntu 24.04，arm-none-eabi-gcc 13.2.1，cmake 3.28.3）；在容器里编译 picotool 和固件；运行主机单元测试；把 `.elf`、`.uf2`、`.elf.map`、`SHA256SUMS`、`picotool-info.txt` 放到 `build/rp2350-hid/`。

板型：pico-sdk 2.3.1 没有 `waveshare_rp2350_usb_c`，本工程自带 [boards/waveshare_rp2350_usb_c.h](boards/waveshare_rp2350_usb_c.h)，按官方原理图写（2 MB W25Q16JV，GP16 WS2812，GP12/GP13 PIO-USB，无 GP25 LED）。

只跑主机单元测试（本机 cc 即可，带 ASan/UBSan）：

```sh
make -C firmware/rp2350-hid/tests
```

测试同时读取共享测试向量 `docs/rp2350-protocol-vectors.json`（与 Windows 测试客户端共用），逐字节核对帧编码、重新同步流，以及固件生成的 INFO/PONG/NAK。

## 依赖版本（固定）

| 依赖 | 版本 | commit |
|---|---|---|
| pico-sdk | 2.3.1 | `079c6f39023649b154152db30f1d781e884879bc` |
| TinyUSB（pico-sdk 子模块） | 0.18.0 | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
| Pico-PIO-USB | 0.7.2 | `3c1eec341a5232640e4c00628b889b641af34b28` |
| picotool（仅用于生成 UF2） | 2.3.1 | `2041936441b48a3cc53ae3da9e805229fe8f4e18` |

Pico-PIO-USB 已固定并由 `fetch_deps.sh` 拉取核对，但当前固件不链接它（见下文）。升级任何一项需要重新跑全部测试并更新本表和 `deps.lock`。

## PIO-USB CDC 通道（控制通道 B）的结论

**不能在同一份 TinyUSB 里同时跑原生 HID 设备和 PIO-USB CDC 设备，这一版关闭该通道。** 依据（都在上面固定的源码里）：

1. TinyUSB 设备栈只有一个实例：`src/device/usbd.c` 中是单个 `static usbd_device_t _usbd_dev` 和单个 `_usbd_rhport`；`class/hid/hid_device.c` 等类驱动写死 `rhport = 0`。一个设备栈只能描述一个 USB 设备。
2. 两种设备控制器驱动互斥：`portable/raspberrypi/rp2040/dcd_rp2040.c` 只在 `!CFG_TUD_RPI_PIO_USB` 时编译，`portable/raspberrypi/pio_usb/dcd_pio_usb.c` 只在 `CFG_TUD_RPI_PIO_USB` 时编译，同一个固件里只能二选一。
3. 绕开 TinyUSB、直接用 Pico-PIO-USB 自带的设备 API（`pio_usb_device_init`）也不够：`pio_usb_device.c` 的 setup 处理只认标准请求和 HID 类请求（SET_REPORT/SET_IDLE/SET_PROTOCOL），不处理 CDC ACM 的 SET_LINE_CODING/SET_CONTROL_LINE_STATE，也没有批量端点收发的上层封装；此外它要求系统时钟是 12 MHz 的整数倍（官方示例设为 120 MHz），与本固件默认 150 MHz 不同。

要做通道 B，可行路线是：在 Pico-PIO-USB 底层上自己实现一个最小 CDC ACM（或 vendor 类 + WinUSB）设备，系统时钟改为 120/240 MHz，PIO-USB 放到核心 1。它需要单独的实现和实机验证，也要处理两个口 VBUS 直连的供电问题（ADR-012 已写明必须使用断开 VBUS 的线）。编译开关 `-DRKMOON_PIO_USB_CDC=ON` 目前会直接报错并说明原因，防止误以为已经支持。

## 源码结构

```
firmware/rp2350-hid/
  CMakeLists.txt            固件工程（Pico SDK 2.x + TinyUSB 设备）
  pico_sdk_import.cmake     SDK 2.3.1 原样拷贝
  deps.lock                 依赖 URL/tag/commit
  boards/waveshare_rp2350_usb_c.h  板型头文件（按官方原理图）
  src/core/                 与硬件无关的 C 代码（主机测试覆盖）
    crc16.[ch]              CRC-16/CCITT-FALSE
    proto.[ch]              帧编码、逐字节解析与重新同步
    bridge.[ch]             通道/会话、输入状态、报告队列（拆分/合并）、看门狗、灯色、PONG/INFO/NAK
  src/main.c                主循环：串口 → core → TinyUSB，USB 状态回传，芯片看门狗
  src/usb_descriptors.[ch]  设备/配置/HID 报告/字符串描述符
  src/tusb_config.h         TinyUSB 配置（3 个 HID 实例，无 CDC）
  src/uart_link.[ch]        UART0 中断接收环形缓冲 + 非阻塞发送
  src/status_led.[ch], ws2812.pio  WS2812 驱动
  tests/                    主机单元测试（Makefile，test_core.c，test_vectors.c，gen_vectors.py）
  tools/Dockerfile, build.sh, fetch_deps.sh
```

## 已知限制

- 未上板。第一次上板要验证：Windows/Linux/macOS 枚举出三个 HID 接口；BIOS 下 boot 键盘/鼠标可用；睡眠唤醒；灯色；1 Mbaud 下连续输入无 rx_errors；拔掉 USB 转 TTL 线后 500 ms 内全释放。
- 键盘 LED 输出报告被忽略，协议 v1 没有回传 Caps Lock 状态的帧。
- boot 协议下相对鼠标只有 3 键、没有滚轮（boot 报告格式所限）。
- 被控机挂起时输入不排队；唤醒后需要主机重发当前状态。
