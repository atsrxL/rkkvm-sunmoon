# ADR-012: RP2350 USB HID 桥（Waveshare RP2350-USB-C）

状态：提议，实现中（2026-09-28）

## 背景

T6 唯一的 USB 设备口（USB-C，FUSB302）位置不便。改用 Waveshare RP2350-USB-C 作为独立 USB HID 设备：板子的原生 USB-C 口接被控电脑，模拟键盘和鼠标；控制端通过串口向板子发送输入事件。

板子规格：RP2350A，150 MHz，520 KB SRAM，2 MB flash；原生 USB 1.1 全速；另有一个 PIO-USB Type-C 母口；15 个 GPIO；2 路 UART；GP16 接 WS2812 指示灯；带 BOOT/RESET 键。

## 连接

- 原生 USB-C → 被控电脑。板子由被控电脑供电，枚举为复合 HID 设备。
- 控制通道 A（必须支持）：UART0，GP0=TX，GP1=RX，3.3 V 电平，1 000 000 baud，8N1。只连 TX、RX、GND，不连 5V/3V3，避免两边电源互相倒灌。T6 端用 USB 转 TTL 模块（3.3 V）插在 T6 的 USB-A 口上，不需要改 T6 的设备树。
- 控制通道 B（尽力支持，**固件 v0.1 未实现，编译开关 `RKMOON_PIO_USB_CDC` 默认关闭**）：PIO-USB 口作为 CDC ACM 设备，一根线直连 T6 或 Windows。依赖 TinyUSB 的 PIO-USB 设备模式，未经实机验证前按“实验”处理。
  - 2026-09-28 固件结论：pico-sdk 2.3.1 自带的 TinyUSB（86ad6e5，0.18.0）只有一个设备栈（`usbd.c` 中单个 `_usbd_dev`，HID/CDC 类驱动固定用 rhport 0），`dcd_rp2040.c` 与 `dcd_pio_usb.c` 由 `CFG_TUD_RPI_PIO_USB` 互斥编译。因此原生口的 HID 设备和 PIO-USB 口的 CDC 设备不能在同一个 TinyUSB 里同时运行。Pico-PIO-USB 0.7.2 自带的独立设备 API 只处理标准请求和 HID 类请求，没有 CDC ACM 的 SET_LINE_CODING 等类请求，也没有批量端点的数据收发封装，并要求系统时钟为 12 MHz 的整数倍（120/240 MHz）。要做通道 B，需要自己在 Pico-PIO-USB 底层实现 CDC（或 vendor 类）并改时钟，工作量和风险都超出“尽力支持”，这版不硬凑。见 firmware/rp2350-hid/README.md。
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
| 0x81 | INFO | proto u8, fw_major u8, fw_minor u8, caps u8（bit0 键盘, bit1 相对, bit2 绝对, bit3 remote wakeup）, 序列号 8 字节 |
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
