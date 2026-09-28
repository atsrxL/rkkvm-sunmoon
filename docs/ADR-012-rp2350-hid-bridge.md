# ADR-012: RP2350 USB HID 桥（Waveshare RP2350-USB-C）

状态：v0.2 通道 B 已实现，编译/主机测试通过，未实机验证（2026-09-28）

## 背景

T6 唯一的 USB 设备口（USB-C，FUSB302）位置不便。改用 Waveshare RP2350-USB-C 作为独立 USB HID 设备：板子的原生 USB-C 口接被控电脑，模拟键盘和鼠标；控制端通过串口向板子发送输入事件。

板子规格：RP2350A，150 MHz，520 KB SRAM，2 MB flash；原生 USB 1.1 全速；另有一个 PIO-USB Type-C 母口；15 个 GPIO；2 路 UART；GP16 接 WS2812 指示灯；带 BOOT/RESET 键。

## 连接

- 原生 USB-C → 被控电脑。板子由被控电脑供电，枚举为复合 HID 设备。
- 控制通道 A（必须支持）：UART0，GP0=TX，GP1=RX，3.3 V 电平，1 000 000 baud，8N1。只连 TX、RX、GND，不连 5V/3V3，避免两边电源互相倒灌。T6 端用 USB 转 TTL 模块（3.3 V）插在 T6 的 USB-A 口上，不需要改 T6 的设备树。
- 控制通道 B（推荐，v0.2 已实现、未实机验证，默认 ON）：PIO-USB CDC ACM 设备直连 T6 Linux 或 Windows 10+，不再需要 USB 转 TTL。UART0 通道 A 保留为备用。
  - 不使用第二份 TinyUSB/dcd：原生口仍使用 TinyUSB + dcd_rp2040；PIO 口在固定 Pico-PIO-USB 0.7.2 设备底层上实现独立 CDC 栈。原始设备上层只支持 HID，不能直接用作 CDC。
  - 引脚（以官方原理图 hardware/schematics/RP2350-USB-C.pdf 及官方示例 examples/C/01_USB/src/pio_usb_configuration.h 为准，仓库 waveshareteam/RP2350-USB-C@774cbb1e）：D+ = GP12（经 R12 27 Ω），D- = GP13（经 R11 27 Ω），即 `pin_dp = 12`、`PIO_USB_PINOUT_DPDM`（DM = DP+1）。GP12/GP13 因此不能再作他用。
  - D+ 上有 R13 1.5 kΩ 固定上拉到 3V3（D- 的 R10 未焊），所以这个口在硬件上本来就是全速设备；固件无法通过软件断开来模拟拔出，只能停止响应。
  - **供电警告**：两个 Type-C 口的 VBUS 都直接接到 VSYS，原理图上没有隔离二极管。如果通道 B 使用普通 USB 线，T6/Windows 的 5V 会和被控电脑的 5V 直接连通，互相倒灌。通道 B 必须使用断开 VBUS 的线或转接头（只接 D+、D-、GND），否则不得同时连接两台主机。通道 A 不受影响。
- 两个通道使用同一协议；同一时刻以最后收到合法 HELLO 的通道为准。

## USB 设备

复合 HID，包含三个接口：

- 键盘：boot 协议，8 字节报告，6 键无冲。
- 相对鼠标：5 键，dx/dy int16（逻辑范围 -32767..32767），垂直滚轮和水平滚轮。接口子类为 boot、协议为 mouse：主机选 boot 协议（BIOS）时，固件改发 3 字节 boot 报告（3 键，dx/dy int8，不含滚轮），大位移同样拆分。
- 绝对指针：5 键，x/y 0..32767，垂直滚轮和水平滚轮。Windows、macOS、Linux 都不需要驱动。

支持 USB remote wakeup：被控电脑睡眠时，任何按键或按钮事件都会触发唤醒（配合 ADR-011）。
VID/PID 可在编译时配置，默认值写在固件 README 中。设备字符串为 "RKMoon HID Bridge"，序列号取芯片唯一 ID。

## 串口协议 v1

帧格式：`0xA5 | type u8 | seq u8 | len u8 | payload[len] | crc16 LE`

- CRC 为 CRC-16/CCITT-FALSE，覆盖 type 到 payload 的全部字节。
- 多字节字段一律小端。
- len 不超过 32。
- 收到错误帧时丢弃并重新寻找 0xA5，同时增加错误计数。
- 重新同步的精确规则：解析器处于寻找状态时，非 0xA5 字节直接跳过，不计错误。遇到 len > 32 或 CRC 错误时，错误计数加 1，**只丢弃这一个 0xA5**，从它后面的下一个字节开始重新寻找（不是丢掉整段 len 字节），这样截断帧不会吞掉紧随其后的合法帧。
- 对已知 type，len 必须与下表 payload 长度完全一致（HELLO 1、KEYBOARD 7、MOUSE_REL 7、MOUSE_ABS 7、其余 0）；不一致时回 NAK 原因 2，不执行。
- seq：主机自由递增（回绕），板子不检查连续性；INFO、PONG、NAK 原样回送所响应请求的 seq，用来匹配请求和应答。
- 鼠标 buttons：bit0 左键、bit1 右键、bit2 中键、bit3 后退、bit4 前进，bit5..7 必须为 0。键盘 modifiers 按 HID 标准：bit0..3 左 Ctrl/Shift/Alt/GUI，bit4..7 右 Ctrl/Shift/Alt/GUI。
- 共享测试向量：`docs/rp2350-protocol-vectors.json`（CRC 校验值、每种帧的编码结果、重新同步流），固件和 Windows 客户端都必须以它为准。

主机发往板子：

| type | 名称 | payload |
|---|---|---|
| 0x01 | HELLO | proto u8 = 1 |
| 0x02 | KEYBOARD | modifiers u8, keys[6] u8（HID usage） |
| 0x03 | MOUSE_REL | buttons u8, dx i16, dy i16, wheel i8, pan i8 |
| 0x04 | MOUSE_ABS | buttons u8, x u16, y u16, wheel i8, pan i8（x、y 为 0..32767） |
| 0x05 | RELEASE_ALL | 空 |
| 0x06 | PING | 空 |

板子回复主机：

| type | 名称 | payload |
|---|---|---|
| 0x81 | INFO | proto u8, fw_major u8, fw_minor u8, caps u8（bit0 键盘, bit1 相对, bit2 绝对, bit3 remote wakeup, bit4 PIO-USB CDC 通道 B）, 序列号 8 字节 |
| 0x86 | PONG | status u8（bit0 USB 已配置, bit1 被控机挂起, bit2 有按键或按钮按下, bit3 看门狗触发过）, rx_errors u16, dropped u16 |
| 0x80 | NAK | 原 type u8, 原因 u8（1 CRC, 2 长度, 3 未知类型, 4 USB 未就绪, 5 协议版本不支持, 6 未建立会话） |

NAK 原因 5、6 为 2026-09-28 固件实现时补充：HELLO 的 proto 不是 1 时回 5；在没有收到合法 HELLO 的通道上、或在已被另一通道的 HELLO 接管的通道上发送 KEYBOARD/MOUSE_REL/MOUSE_ABS/RELEASE_ALL 时回 6，不执行（PING 在任何通道都应答）。主机遇到 6 应重新发送 HELLO。客户端遇到未知原因码应只显示数字，不视为协议错误。

USB 未就绪（原因 4）的具体规则：未配置时一律回 4；被控机挂起时，键盘、鼠标帧回 4 且不排队（避免唤醒后重放旧输入），其中带有按键或按钮按下的帧会触发一次 remote wakeup（100 ms 内最多一次），纯移动不唤醒。唤醒后主机应重发当前完整状态。

CRC 错误时原帧的 type/seq 不可信，NAK 的原 type 和 seq 只作诊断参考；主机不得据此重发输入帧（重发会造成重复按键），以下一次完整状态帧为准。

板子对每个合法帧（HELLO 和 PING 以外）不单独回复，以免拖慢输入；HELLO 回 INFO，PING 回 PONG，失败回 NAK。

## 安全释放

- 看门狗：只要有按键或按钮处于按下状态，且超过 500 ms 没有收到任何合法帧，固件就自动发送全释放报告，并在 PONG 中置 bit3。主机空闲时应每 200 ms 发一次 PING。
  - “合法帧”指当前活动通道上 CRC 正确且长度正确的帧；非活动通道的 PING 不喂狗。bit3 保持到下一次合法 HELLO 才清除。
  - 被控机进入挂起时固件也立即全释放（释放报告在恢复后发出）。USB 重新枚举时，固件视作主机已无任何按下状态。
  - 另有芯片硬件看门狗（1 s）：主循环卡死时芯片复位，USB 断开，被控机自然释放全部按键。
- 收到 RELEASE_ALL、HELLO、控制通道切换时，也立即全释放。
- 相对鼠标的 dx/dy 超过单个 HID 报告的范围时，由固件拆分，不丢弃。
- 报告队列满时，合并相邻的相对移动，不丢弃按键和按钮的变化。
  - 实现细节：每个接口一个报告队列（键盘 32、相对 16、绝对 16）。相对/绝对队列只剩 1 个空位时，按钮相同的新移动并入队尾（相对量累加，绝对坐标取最新），最后一个空位留给按钮变化。键盘报告从不合并。任一队列满时固件暂停读取串口（字节留在 2 KB 接收缓冲里，形成背压），不会丢弃按键；只有接收缓冲溢出才计入 PONG 的 dropped。
  - 半帧超时：一帧的字节间隔超过 50 ms 时丢弃这半帧并计 1 次 rx_errors。

## 与现有系统的关系

T6 服务端将来新增一个 `rp2350` HID 后端，替换 kvmd，但保留独占租约和释放失败阻断机制。这一步单独实施，并单独做实机验证。Windows 测试客户端直接通过串口使用本协议，用来在没有 T6 的情况下验证板子。

## v0.2 通道 B 实施决策（编译/主机测试，未上板）

- 默认 RKMOON_PIO_USB_CDC=ON；OFF 不链接 PIO CDC、不启动 core1、保持原 150 MHz/UART/HID 行为，固件版本仍升为 0.2，INFO caps=0x0f。ON caps=0x1f，INFO 长度、协议 v1 均不变。旧 0.1 客户端忽略未知能力位，Mac Qt 12/12 验证通过，无客户端代码修改、无 Windows 重建。
- ON 使用 120 MHz：12 MHz 整数倍且不超出 RP2350 额定 150 MHz；不选需要超频的 240 MHz。原生 USB 的 PLL_USB/clk_usb 仍为 48 MHz。SDK set_sys_clock_khz 默认使 clk_peri=48 MHz，UART 1 Mbaud 分频=3；即使配置为跟随 sys 的 120 MHz，分频=7.5，同样精确。WS2812 10 周期/bit，120 MHz / (800 kHz × 10)=15，保持原脉宽。
- core0：TinyUSB dcd_rp2040、UART0、协议仲裁、HID 队列、500 ms 看门狗；core1：PIO packet IRQ、独立 EP0/CDC、bulk 传输。PIO0 SM0 TX；PIO1 SM0 NRZI RX、SM1 EOP；DMA0 TX，由库 claim；WS2812 固定 PIO2 SM0、无 DMA。每个 PIO 程序空间独立。
- CDC：IAD + 控制接口 0 + 数据接口 1，配置总长 75，EP0 64，EP81 interrupt 16/16ms，EP02 OUT 与 EP82 IN bulk 64。产品 RKMoon RP2350 Control，接口 RKMoon Control CDC；VID 默认 1209，PID 默认 0002（原生 HID 0001），仅内部测试标识；正式分发需合法 VID/PID。序列号为芯片 8 字节 ID 的 16 字符大写十六进制。自供电、无控制口 remote wakeup。
- 实现 GET_DESCRIPTOR/STATUS/CONFIGURATION/INTERFACE、SET_ADDRESS/CONFIGURATION/INTERFACE、端点 HALT/CLEAR_FEATURE；未知/不支持请求 STALL。CDC SET/GET_LINE_CODING、SET_CONTROL_LINE_STATE，保存线编码但忽略波特率。DTR 必须置位；interrupt SERIAL_STATE 通知随 DTR 更新。显式处理控制与 bulk 的 ZLP。
- RX/TX 各 2048 字节 SPSC，C11 lock-free 32-bit acquire/release。core1 只在 RX 至少有 64 字节空位时 arm OUT，否则硬件 NAK；core0 在 HID 队列满或 TX 可用空间不足 512 时暂停解析，预留重同步可能产生的多个 NAK。bulk OUT 有完整包空间才 ACK，TX 64 字节包后发 ZLP。该策略不丢弃正常按键边沿；链路失效时有意废弃未处理旧会话字节。
- SOF 停止 3 ms 视为挂起/拔线；断 VBUS 线没有独立 VBUS 检测，无法区分二者。USB reset、DTR 掉或 SOF 消失使 epoch 增加、offline；core0 撤销 B 会话并排空 RX，确认 epoch 后 core1 排空 TX 才恢复 online。核心 1 心跳超过 10 ms 未更新也撤销会话，并要求 core1 恢复时清空旧数据。失效不喂狗，保持现有最后合法帧后超过 500 ms 释放规则；UART 会话不被非活动 B 失效打断。恢复必须重新 HELLO，PING 不能重新获取租约。
- 最后合法 HELLO 仍决定通道，切换先全释放。实际 USB 主机消费释放报告仍需实测。
- patches/pio-device.patch 精确替换上游 HID 设备上层，保留包引擎；补充 IN 仅 ACK 后前进、OUT DATA PID 去重/容量检查、SETUP 长度/持久副本、地址状态阶段、非阻塞 reset、SOF 时戳、移除 GP3/GP4 调试写、部分接收等待超时及接收缓冲上界。构建检查 commit + 两个原始文件 SHA256，再在构建目录 git apply --check；不修改共享依赖 checkout，不放宽未知版本。ACK 重试、电气时序、异常线状态仍需真板验证。

**供电警告再次强调：两个 Type-C 的 VBUS 在板上直连 VSYS。PIO-USB 口必须用已确认断开 VBUS 的数据线/转接头，只接 D+/D-/GND；不得同时用两根普通 USB 线连接两台主机。不要误用仅供电的 USB 数据阻断器。**

验收与产物摘要见 results/20260928-rp2350-cdc/STATUS.md。本阶段仅编译/主机测试，未上板，未访问 T6。
