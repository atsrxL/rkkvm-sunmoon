# RKMoon RP2350 HID 桥固件

Waveshare RP2350-USB-C 上运行的 USB HID 桥固件，协议与硬件设计见 [ADR-012](../../docs/ADR-012-rp2350-hid-bridge.md)。

> **状态：仅编译验证，未上板。** 板子、USB 枚举、被控机输入、remote wakeup、WS2812 灯色、串口实际收发都没有在真实硬件上测试过。主机端单元测试覆盖协议、CDC 请求/描述符、跨核缓冲与传输状态模拟，不代表 PIO 电气时序通过。

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
- 控制通道 B（推荐）：PIO-USB CDC ACM，默认开启，固件 v0.2，**已实现、未实机验证**。

## 刷写

1. 按住板子上的 **BOOT** 键，把板子的原生 USB-C 口插到电脑上，然后松开。
2. 电脑上会出现一个名为 `RP2350` 的 U 盘。
3. 把 `rkmoon_rp2350_hid.uf2` 拖进去。拷贝完成后板子自动重启，U 盘消失。

已经刷过本固件的板子，也可以按住 BOOT 再按一下 RESET（RUN）进入同样的 U 盘模式。

## 接线

- 板子原生 USB-C 口 → 被控电脑。板子由被控电脑供电。
- 控制通道 A（备用）：T6 的 USB-A 口插一个 **3.3 V** USB 转 TTL 模块（CH340/CP2102/FT232 均可，必须是 3.3 V 电平），**只接三根线**：

| USB 转 TTL 模块 | RP2350-USB-C |
|---|---|
| TXD | GP1（UART0 RX） |
| RXD | GP0（UART0 TX） |
| GND | GND |

  不要接模块的 5V 或 3V3：板子已由被控电脑供电，两边电源互接会倒灌。TX 和 RX 要交叉。T6 上不需要改设备树。

- 控制通道 B（推荐，无需 USB 转 TTL）：PIO-USB Type-C → 控制端 T6 Linux 或 Windows 10+，D+=GP12、D-=GP13（DM=DP+1，D+ 板载 1.5k 上拉）。

| 通道 B 数据线 | 连接 |
|---|---|
| D+ / D- / GND | 保持连通 |
| VBUS / 5V | **必须断开，连接前用万用表确认** |

**两个 Type-C 的 VBUS 在板上直连 VSYS；禁止用两根普通 USB 线接两台主机。** 先接原生口供电，再用断 VBUS 数据线接 PIO 口；不是只供电的“数据阻断器”。控制端应枚举 COM 或 /dev/ttyACM*，打开后置 DTR，再发 HELLO。现有 Windows 测试客户端已经置 DTR，可直接选择该 COM 口。

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

脚本会：按 `deps.lock` 拉取并核对依赖 commit（放在 `build/rp2350-hid-deps/`，不入库）；构建 Docker 镜像 `rkmoon-rp2350-build:1`（Ubuntu 24.04，arm-none-eabi-gcc 13.2.1，cmake 3.28.3）；在容器里编译 picotool 和固件；运行主机单元测试；把 `.elf`、`.uf2`、`.elf.map`、`SHA256SUMS`、`picotool-info.txt` 放到 `build/rp2350-hid-ON/ 或 build/rp2350-hid-OFF/`。

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

Pico-PIO-USB 已固定并由 fetch_deps.sh 拉取核对；ON 使用设备底层的精确补丁副本，不修改共享依赖 checkout。升级任何一项需要重新跑全部测试并更新本表和 `deps.lock`。

## PIO-USB CDC 通道（控制通道 B）的结论

v0.1 的限制仍成立：TinyUSB 0.18.0 只有单设备栈，dcd_rp2040 与 dcd_pio_usb 互斥。v0.2 因此保留原生口 TinyUSB，PIO 口改用独立最小 CDC 栈，不链接第二个 dcd。

- 固件 0.2，INFO caps bit4 表示 B；协议 v1 和 INFO 12 字节保持兼容。
- 系统 120 MHz（不超频）；原生 USB 48 MHz，UART 精确 1 Mbaud；WS2812 分频 15、800 kHz。
- core1 专用 PIO USB，core0 原有主循环。PIO0 SM0 TX / DMA0，PIO1 SM0 RX + SM1 EOP；灯 PIO2 SM0、无 DMA。
- CDC 1209:0002、产品 RKMoon RP2350 Control、唯一芯片序列号，与 HID 1209:0001 不同。两个 PID 仅内部测试。IAD + 两接口，EP81 通知，EP02/EP82 bulk 64。
- RX/TX 各 2 KiB 无锁单生产者/单消费者缓冲，OUT 未有整包空间则 NAK；回复空间不足则暂停解析。DTR、reset、SOF 丢失触发 epoch 清理，旧字节不跨会话重放；core1 无响应也撤销 B 会话。恢复要重新 HELLO，现有 500 ms 看门狗全释放规则不变。
- CDC 请求、EP0 状态阶段、bulk ZLP、端点 halt、SERIAL_STATE 已实现；详细补丁及测试边界见 ADR-012。

编译两个配置：

    firmware/rp2350-hid/tools/build.sh
    RKMOON_PIO_USB_CDC=OFF firmware/rp2350-hid/tools/build.sh

OFF 保持原 UART/HID 路线与 150 MHz，不含 CDC/core1，版本仍为 0.2。构建结果与 SHA256 见 [本轮记录](../../results/20260928-rp2350-cdc/STATUS.md)。**两种配置均仅编译/主机测试，未上板。**

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
  src/pio_cdc.[ch]          独立 CDC 设备栈与 core1/跨核传输
  src/core/cdc_control.[ch], spsc.h  CDC 请求/描述符与无锁缓冲
  patches/pio-device.patch  固定上游设备底层精确补丁
  tests/                    主机单元测试（Makefile，test_core.c，test_vectors.c，gen_vectors.py）
  tools/Dockerfile, build.sh, fetch_deps.sh
```

## 已知限制

- 未上板。第一次上板要验证：Windows/Linux/macOS 枚举出三个 HID 接口；BIOS 下 boot 键盘/鼠标可用；睡眠唤醒；灯色；1 Mbaud 下连续输入无 rx_errors；拔掉 USB 转 TTL 线后 500 ms 内全释放。
- 键盘 LED 输出报告被忽略，协议 v1 没有回传 Caps Lock 状态的帧。
- boot 协议下相对鼠标只有 3 键、没有滚轮（boot 报告格式所限）。
- 被控机挂起时输入不排队；唤醒后需要主机重发当前状态。

## 首次上板验证清单（全部待验证）

1. 两端 USB 均断开。确认 PIO 数据线/转接头 VBUS 不通，D+/D-/GND 连通；不要仅按商品名判断。
2. BOOT + 原生口刷入 0.2 UF2。原生口接被控电脑供电，确认三个 HID 接口；此时 PIO 口保持断开。
3. 用断 VBUS 线连接 PIO 口到 Windows 10+ / Linux 控制端，验证 usbser COM / cdc_acm、产品与唯一序列号；反向插头也要测试。
4. 客户端选 COM，保持默认 1 Mbaud（CDC 忽略波特率），连接应显示固件 0.2、INFO caps=0x1f；验证 PING/PONG 与键鼠。
5. 验证 64/128 字节边界、ZLP、长时间连续输入、主机暂停读取、不同 USB Hub、丢 ACK 重试无重复输入；真实时序不能从主机模拟推断。
6. 按键期间拔控制线、关闭端口使 DTR 掉、控制机睡眠：最后合法帧后约 500 ms 全释放。恢复后重新连接/HELLO；确认旧字节不重放。
7. UART A/B 交替 HELLO，旧通道不能继续输入；500 ms 释放、原生 USB 挂起唤醒、BIOS boot 键鼠、WS2812 灯色与 UART 1 Mbaud 均回归。

没有 VBUS 感知且 D+ 固定上拉，固件无法主动电气断开；挂起与拔线通过 SOF 消失统一处理。PIO 全速时序、Windows 枚举与高负载稳定性仍是主要实机风险。
