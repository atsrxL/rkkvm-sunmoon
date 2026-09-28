# RP2350 控制通道 B — 2026-09-28

基线 main c8c9cde，当前工作区改动，未提交。固件 0.2。**编译/主机测试，未上板。** 未访问 T6、未改服务端/client/、未改 Windows 客户端、未启动远程 VM。

| 验证 | 结果 | 边界 |
|---|---|---|
| Docker tools/build.sh ON | 通过 | arm-none-eabi-gcc 13.2.1 / pico-sdk 2.3.1，text 47904 / data 0 / bss 19436 B |
| Docker tools/build.sh OFF | 通过 | text 28424 / data 0 / bss 6044 B；原 150 MHz/UART/HID 路线 |
| Mac clang + Docker GCC ASan/UBSan | 通过 | ON/OFF 核心各 200718 checks，0 failures，含共享向量 |
| CDC 主机测试 | 通过 | 描述符链/长度/端点、类/标准请求、10万随机 setup、SPSC 100万字节跨线程及 uint32 回绕、通道切换/失效 |
| CDC 传输模拟 | 通过 | 生产 pio_cdc.c + 显式控制器桩；EP0 状态/中止/STALL、配置、bulk/ZLP、DTR epoch、halt/reset；不是 PIO 硬件测试 |
| 原客户端 Mac Qt 6.11.2 | 通过 | 12/12，包含 0.2 CDC INFO；客户端源码未改，未重新 Windows 构建 |
| 实际 CDC 枚举/USB 时序/输入/拔线释放 | 未测试 | 无真板，不得从编译/模拟推导 |

原生口继续 TinyUSB dcd_rp2040；PIO 独立 CDC 不占用第二个 TinyUSB 栈。系统 120 MHz、core1 PIO；PIO0 SM0 TX + DMA0、PIO1 SM0 RX/SM1 EOP，WS2812 PIO2 SM0。RX/TX 各 2KiB SPSC，完整包空间不足时 NAK，回复空间不足停止解析。CDC 1209:0002（内部测试），IAD 两接口，bulk 64；HID 描述符未改。时钟计算、补丁原理、接线和上板清单见 ADR-012 与固件 README。

第一次构建在更新前 INFO 0.1 向量处失败；旧 Qt 测试也曾因应答向量数量写死为 5 而失败。已保留主表数量、主表 INFO 更新为 0.2 CDC，并在 firmware_info_variants 扩展表保留旧 0.1 与 0.2 UART-only；最终全部通过。失败/完整日志保存在 private/results/local/20260928-rp2350-cdc，不上传硬件或秘密日志。

产物完整 SHA256 见 artifacts.json、ON-SHA256SUMS、OFF-SHA256SUMS。默认交付 ON UF2，OFF 仅留本地备用。交付核对见 delivery.json。

风险：PIO 包引擎的实时响应和重试只能上板验证；固定 D+ 上拉不能软件拔出，无 VBUS 感知，SOF 停止统一视为挂起/拔线。首次必须测试 Windows 10+/Linux 枚举、64N/ZLP、不同 Hub、持续负载/背压、拔线/DTR/挂起、A/B 抢占、原生 boot HID/wakeup/UART/WS2812。恢复后必须重新 HELLO。

**两个 Type-C 的 VBUS 直连 VSYS：先验证控制口线缆 VBUS 确已断开，仅 D+/D-/GND 连通；刷写并接原生口到被控电脑供电后，再连接控制口。禁止两根普通 USB 线同时接两台主机。**

收尾：构建 Dockerfile 中旧 rm 命令已改为 Python shutil，重建镜像后 ON/OFF 全部再次通过且 UF2/ELF SHA256 不变。临时脚本、Qt 测试构建树与主机测试可重建文件已清理，完整日志保留 private；复用的依赖、Docker 镜像及请求交付的 ON/OFF 产物保留。构建容器 --rm 自动退出，未操作原有常驻容器。
