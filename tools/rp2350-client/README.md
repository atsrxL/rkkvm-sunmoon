# RKMoon RP2350 HID 桥 Windows 测试客户端

这个工具在没有 T6 的情况下，直接通过串口使用 RP2350 HID 桥的协议（docs/ADR-012-rp2350-hid-bridge.md，协议 v1）控制被控电脑，同时检查板子是否工作正常。Qt 6.8.3 Widgets + QSerialPort 实现，协议编码和 CRC 放在不依赖界面的 src/rp2350_protocol.*，以后可以直接复用到主客户端。

## 接线

- 板子原生 USB-C 口接被控电脑。
- 控制通道 A（推荐）：USB 转 TTL 模块（3.3 V）插在控制电脑上，模块 TX→板子 GP1(RX)，模块 RX→板子 GP0(TX)，GND→GND。不要连 5V/3V3。
- 控制通道 B（实验）：板子的 PIO-USB Type-C 母口（D+=GP12，D-=GP13）作为 CDC 串口。原理图上两个 Type-C 口的 VBUS 都直接接到 VSYS，所以这里必须用断开 VBUS 的线或转接头，否则控制电脑和被控电脑的 5V 会直接连在一起。

## 使用

1. 运行 RKMoon-RP2350-Client\rkmoon-rp2350-client.exe（文件夹解压即用，不需要安装 Qt）。
2. 在“串口”里选 COM 口（显示描述和 VID:PID，可点“刷新”），波特率默认 1000000，可选或手动输入其他值。
3. 点“连接”：客户端打开串口（8N1，无流控，置 DTR）并发送 HELLO，如果 500 ms 内没有 INFO 就重试，最多 6 次。收到 INFO 后显示协议版本、固件版本、能力位（键盘/相对鼠标/绝对鼠标/远程唤醒）和 8 字节序列号。
4. 连接期间每 200 ms 发一次 PING。PONG 一行显示 USB 是否已配置、被控机是否挂起、是否有按键按下、看门狗是否触发过、接收错误计数、丢弃计数和往返时间。超过 1 秒没有任何应答时，“链路”显示“无应答”。NAK 会写进下方日志。
5. 捕获：先选“相对鼠标”或“绝对鼠标”，再单击捕获区开始捕获。
   - 键盘：按扫描码转成 HID usage，左右 Ctrl/Shift/Alt/Win 分开处理，AltGr 附带的假左 Ctrl 会被丢弃，Pause/NumLock/PrintScreen 按 VK 处理。捕获期间安装低级键盘钩子，Win 键、Alt+Tab 等只发给被控机。
   - 相对模式：隐藏光标并把它锁在捕获区中心，位移累积后按 int16 拆分发送。
   - 绝对模式：捕获区宽高映射到 0..32767（左上 0，右下 32767），对应被控机的整个屏幕。
   - 支持 5 个鼠标键、垂直滚轮和水平滚轮（每 120 为一格，余数保留）。
   - 按 Ctrl+Alt+Shift+Z（左右任意）释放捕获，Z 不会发给被控机。
   - 窗口失去焦点、切换鼠标模式、断开连接或关闭窗口时都会自动释放捕获，并发送 RELEASE_ALL。
6. “释放全部”：停止捕获并立即发送 RELEASE_ALL。
7. “测试：输入 "rkmoon"”：每 30 ms 一次按下/松开，逐个字母发给被控机。可以在被控机上打开记事本来检查结果。

串口积压超过 256 字节时，鼠标位移只累积不发送，积压消失后合并发出；按键和按钮变化始终立即发送。

## 隐私

客户端不记录按键内容：日志只写连接、捕获开始/释放、NAK 和统计值，从不写按键 usage 或字符。

## 构建与测试

协议测试向量：docs/rp2350-protocol-vectors.json，固件和客户端共用，其中包括 CRC 校验值、每种帧的编码结果，以及重新同步的数据流（错误 CRC、len>32、截断帧）。

Mac（Homebrew Qt 没有 QtSerialPort，只跑核心测试）：

    mkdir /tmp/rp2350-core && cd /tmp/rp2350-core
    qmake6 <repo>/tools/rp2350-client/tests/core-tests.pro && make && ./rp2350-core-tests

Windows（Qt 6.8.3 msvc2022_64，需要 qtserialport 模块，在 VS2022 x64 环境中执行）：

    pwsh -File tools\rp2350-client\build_windows.ps1 -QtRoot <Qt\6.8.3\msvc2022_64> -SourceRoot <repo> -BuildRoot <新目录> -OutputDirectory <新目录>

脚本依次执行 qmake/nmake、完整 QtTest（offscreen，包括捕获区和窗口测试）、windeployqt 打包、复制 VS2022 CRT、在干净 PATH 下做 4 秒启动冒烟，最后生成 build-manifest.json（源码 SHA256）和 delivery-manifest.json（每个文件的 size/sha256）。

## 验证边界

目前只有离线测试：协议向量、键位映射、输入状态、捕获区的合成事件和打包后启动。还没有接真板子，也没有验证真实 COM 口、HID 枚举、被控机输入或看门狗。

