# 验收矩阵

## 八模式 EDID 输入兼容性

2026-09-24：最终8模式（首选1080p120）全部通过Znas RTX5060Ti→T6的实际输入锁定；不是新增模式的采集/编码/客户端验收。EDID已临时写入、未开机持久化；实际输出回到1080p60。详见 [逐项结果](EDID-EIGHT-MODES.md)。

通用源模式服务端（ADR-009）已部署T6。真实采集+MPP硬编+独立解码：1440p120（HEVC/H.264）、1600p120、1280p90、4K60、4K30、1080p60、1080p30 通过；1080p120 未通过（CPU拷贝路径约108fps）。Moonlight客户端在这些新模式下的播放尚未验收。见 [扫测](../results/20260924-general-modes/HARDWARE-SWEEP.md)。

## 当前自动画面模式任务

输入延迟与断线缓冲（ADR-010）：HID 改为持久 kvmd WebSocket、worker 采集5秒内自愈、bridge 丢帧等IDR。已构建（离线117项通过）并部署T6（rkmoon-kvm 8c7cbf8a…，worker 647c109e…）。客户端实测鼠标延迟和断线恢复：未测试。见 [ADR-010](ADR-010-input-stream-recovery.md)。

2026-09-24 用户新增要求：Ctrl+Alt+Shift+C 显示本地指针；客户端自动跟随 HDMI 尺寸/帧率，设置仅有码率和编码。实现与本轮构建/部署证据单列于 [自动跟随记录](../results/20260924-1440p90/DISPLAY-FOLLOW.md)，不能把下方旧手动预设验收当作新功能验收。

## 1440p90当前阶段

| 项目 | 状态 | 证据/限定 |
|---|---|---|
| 实验server/worker完整构建与离线回归 | 通过 | Linux ARM64容器完整链接；30原生通过，93 Python/联合中91通过2跳过，配置gate15项通过；见[构建记录](../results/20260924-1440p90/SERVER-BUILD.md)，非实机编码 |
| NVIDIA输出与T6锁定 | 通过 | 两次2560×1440、89.9935Hz，临时EDID/NVAPI，见NVIDIA专项记录 |
| 完整V4L2缓冲采集30s | 通过 | 2695帧、timestamp89.9981fps、每帧11059200字节、无缺帧/错误；无编码，见[采集记录](../results/20260924-1440p90/CAPTURE.md) |
| 真实HEVC/H264 90fps编码与独立解码 | 通过 | 各60s/5394帧，89.998/89.982fps；DMA-BUF，HEVC0skip/H2641skip；仅当前较静态源，见[编码记录](../results/20260924-1440p90/ENCODE.md) |
| 客户端1440p90预设/请求参数 | 通过 | Mac arm64 Qt12/12、Python3/3；实验模式，默认仍1080p60 |
| 新Windows90Hz手动预设包 | 通过 | bac1b3e776b1，Qt Windows12/12，Target读回SHA256 06381939e037dc79b13060f706959ee243b5e639e43d809ec26f3977fe9ef07e；不含本轮自动跟随功能 |
| 真实90fps客户端视频/音频/USB | 未测试 | 不从采集结果推导端到端性能 |

以下时间顺序记录保留历史状态；以上矩阵为本阶段最新验收。

> 1440p90后续突破：NVIDIA NVAPI自定义输出成功，两次T6时序探测均为2560×1440、89.9935Hz，接收锁定通过。下条BADMODE为历史失败。持续采集、MPP编码及客户端90fps仍未测试；见 [NVAPI实测](../results/20260924-1440p90/NVIDIA.md)。

> 1440p90专项：EDID试验未能使Znas直通RTX5060Ti接受90Hz，Windows模式测试BADMODE(-2)。原EDID已恢复、实际1080p59.9952输入正常；90Hz接收/采集/编码/客户端均未测试通过，代码仍保留60Hz限制。见 [证据](../results/20260924-1440p90/STATUS.md)。

> 最新要求：HTTP＋密码，取消HTTPS/证书验证；默认密码 `kvm`，Windows为主要客户端。此条取代下文旧TLS/TOFU要求，见 [ADR-005](ADR-005-http-password.md)。

> 2026-09-24：按用户要求改为 IP＋密码（初版 `kvm`），取消 PIN/pair 和客户端证书登记授权。TLS/输入加密保留；旧配对说明及旧实机通过记录不代表新认证验收。见 [认证决策](ADR-004-password-auth.md)。新版本尚未部署到设备。

> 实机更新：ARM64 worker/Sunshine 已构建；T6 真实 1080p60 HEVC/H264 各 60 秒及独立解码通过；原版 Moonlight HEVC 硬解超过 11 分钟、H264 重连通过。USB 尚未完成。见 [T6 验收记录](../results/20260923-t6/STATUS.md)。

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](../results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

状态必须为：通过 / 失败 / 未测试 / 部分完成。每次结果绑定具体硬件、commit、二进制/动态库、客户端和证据目录。原记录不是新验收。

## 当前HTTP基础密码版本

服务端完整构建、28原生、91/93工具测试（2跳过）、生产binary HTTP认证、无HTTPS监听/证书生成及重启身份回归通过。Windows客户端构建由子代理执行，最终交付另记；未部署T6。见 [HTTP版本证据](../results/20260924-http-password/STATUS.md)。

## 2026-09-24 密码认证版本

| 检查 | 状态 | 证据 / 限定 |
|---|---|---|
| P0 ARM64 Linux完整构建 | 通过 | [记录与hash](../results/20260924-password/STATUS.md)，隔离容器，非 T6 原生 |
| 密码认证及无pair网络回归 | 通过 | 生产binary + 合成probe worker，真实HTTP/HTTPS；正确/错误/缺失密码、pair关闭、重启身份稳定，无媒体流 |
| 原生/联合/脱敏回归 | 通过 | 28原生；93工具/联合中91通过2内核fixture跳过；14 overlay、5脱敏；生产密码guard C++测试 |
| 新客户端本地构建/认证 | 通过 | macOS ARM64链接、3源码组、10/10 Qt含loopback TLS；详见 [客户端验收](../client/docs/ACCEPTANCE.md) |
| 新客户端＋真实 HDMI/音频/USB | 未测试 | 本次未访问设备，旧实机通过记录不迁移 |

## 最小服务端独立分支（`feat/minimal-kvm-server`，不覆盖已实测 main）

| 检查 | 状态 | 证据 / 限定 |
|---|---|---|
| 原版 T6 HDMI/MPP、Moonlight 视频及 P3 USB 基线 | 通过 | 仅 `main` 的 results/20260923-t6/STATUS.md；不作为本分支的新测试 |
| 独立 `rkmoon-kvm` P0 构建与无Web启动 | 通过 | 冻结912613c ARM64 QEMU链接exit0；隔离模拟probe启动TCP仅47984/47989/48010，无47990；见 results/20260923-minimal/STATUS.md。不是实机HDMI |
| 私有 Unix PIN 操作与同 UID 权限 | 部分完成 | 分包/取消/边界fixture和真实Unix LIST、216ms退出通过；未完成新binary真实客户端PIN握手 |
| HDMI ALSA 48k 双声道→Opus/RTP 音频 | 部分完成 | 真实生产object合成ALSA null+注入stale/xrun/初次无源5/10/20ms、Opus解码/恢复通过；真实HDMI PCM/声音/同步/重连未测，父会话发现T6 HDMI PCM未暴露，仍是阻碍 |
| 无音频源不阻止视频/输入 | 未测试 | 代码路径保留视频入口；尚无本分支真实会话 |
| 定制客户端真实视频+声音+USB | 未测试 | 等待客户端子会话与协调测试窗口 |

本分支P0已通过（912613c，25原生+87 Python/联合/工具），新P1/P2/P3仍未测试，由父会话协调实机窗口。项目构建 VM301 为 Debian x86_64客体+arm64 QEMU容器；不是T6原生执行。最小服务端的“通过”只适用于明确列出的P0及模拟启动，不能声称新HDMI/客户端闭环已实测。

## 本次云端

| 检查 | 状态 | 证据 / 限定 |
|---|---|---|
| 架构入口、固定版本、许可清单 | 部分完成 | ADR/SOURCES，源码可读；未全量安全审计或全链路验证 |
| 自有核心+V4L2 probe构建 | 通过 | results/cloud/configure.log、build.log；x86_64 Linux |
| worker CLI独立编译 | 通过 | results/cloud/worker-cli-compile.log；未链接encoder_mpp.cpp |
| 原生25项 | 通过 | results/cloud/ctest.log |
| Python+原生输入联合+工具68项 | 通过 | results/cloud/python-tests.log；fake USB backend |
| ASan/UBSan原生25项 | 通过 | results/cloud/asan-tests.log |
| 完整Sunshine套补丁/编译链接 | 未测试 | 容器无法解析GitHub，仅连接器可读；不是已成功的patch |
| 真实SDK的encoder_mpp.cpp编译链接 | 未测试 | 容器无MPP SDK/板卡 |
| T6只读基线 | 未测试 | 用户尚未提供当前设备结果 |
| 60秒真实HEVC/Main8/H264编码与独立解码 | 未测试 | 不采用旧H264结果填表 |
| 1080p60原版Moonlight配对/硬解/10分钟 | 未测试 | 客户端资料与设备均未取得 |
| 实际USB相对输入与断连释放 | 未测试 | 旧记录绝对模式是P3阻碍，不属于新已修复结果 |
| 物理端到端/高分辨率/长稳 | 未测试 | 没有测量数据 |

## 本地待填环境

- 板型/内存/镜像/内核/散热：未确认。
- HDMI节点/源尺寸cadence/plane/stride/sizeimage/color range/transfer：未确认。
- MPP头版本/运行动态库路径与SHA256：未确认。
- 新Sunshine binary SHA256/子模块版本/补丁hash：未确认。
- kvmd版本/socket鉴权/mouse.absolute/既有gadget所有权：未确认。
- Moonlight系统/version/GPU/decoder/屏幕刷新率/网络：未确认。
- 采集与输入独占授权范围/窗口/旧服务恢复方法：未确认。

## 本地阶段记录模板

| 时间/阶段 | 环境ID | 检查 | 状态 | 证据路径/hash | 未完成/回滚 |
|---|---|---|---|---|---|
| 待执行 | 待填 | P0-build | 未测试 | — | — |
| 待执行 | 待填 | H0/P1-HEVC/P1-H264 | 未测试 | — | — |
| 待执行 | 待填 | P2-video-only | 未测试 | — | — |
| 待执行 | 待填 | P3-USB | 未测试 | — | — |

一次fail不能改成“部分通过”掩盖致命问题。软件解码是独立验证工具，不证明客户端硬解；MPP黑帧成功不证明HDMI；client连接成功不证明尺寸、编码或输入正确。

## 2026-09-24：服务端正式安装

| 检查 | 状态 | 说明 |
|---|---|---|
| 发布包生成（package_server.py，T6 本机） | 通过 | 20260924-r9，MANIFEST 逐文件 SHA256 |
| install_server.py 安装、enable、启动 | 通过（实机） | 停旧服务后 HID 恢复标记不存在再切换 |
| EDID 开机服务 | 通过（实机，未重启） | EDID 已一致，未重写 |
| 47989 密码认证、服务器身份保持 | 通过（实机） | 正确密码返回原 uniqueid，错误密码 401 |
| 客户端出画面/声音/键鼠 | 未测试 | 需用户用 Windows 客户端连接 |
| T6 整机重启后自动恢复 | 未测试 | 按用户要求暂不重启 |

## 2026-09-25：无信号会话（ADR-011）与客户端信息面板

| 检查 | 状态 | 说明 |
|---|---|---|
| worker/Sunshine ARM64 构建（VM301） | 通过 | worker 990f5377…，rkmoon-kvm 4fec78c4… |
| 发布 20260925-r10 安装并启动 | 通过（实机） | tar.gz 0fdafa31…，四个服务 active，serverinfo 正常报告 no_signal |
| worker 占位模式（T6 实机，真实 MPP，IPC 协议驱动） | 通过（实机） | 5 秒 51 帧 ≈10fps，首帧 IDR 841B，P 帧 ≤42B，stop 正常退出 0；未打开 HDMI 设备 |
| 客户端 Connect 只显示信息、Start/无信号可启动 | 通过（Mac 离线） | Qt 26/26 |
| 无信号经客户端进入黑屏并用键鼠唤醒、信号出现后切到真实模式 | 未测试 | 需用户用新 Windows 客户端实测 |

## RP2350 HID 桥固件（ADR-012，2026-09-28）

| 项目 | 状态 | 证据/限定 |
|---|---|---|
| 协议解析/CRC/看门狗/拆分合并 主机单元测试 | 通过（离线） | Mac clang 与 Docker gcc 13.3，ASan/UBSan，200712 项检查 0 失败，含共享向量 docs/rp2350-protocol-vectors.json |
| 固件交叉编译（pico-sdk 2.3.1，板型 waveshare_rp2350_usb_c，自带头文件） | 通过（仅编译） | arm-none-eabi-gcc 13.2.1；uf2 351562e6…，elf 08eb4e42…；text 28424 B |
| Target 交付 RKMoon-RP2350-HID | 通过 | uf2 读回 cmp 一致，SHA256 351562e62cef259720ef57986ffc82f80e8240e176c7db1c388fb7354785da31 |
| 上板：枚举、键鼠输入、boot 协议、remote wakeup、灯色、UART 1 Mbaud、500 ms 释放 | 未测试 | 未上板 |
| PIO-USB CDC 控制通道 B | 部分完成 | v0.2 独立 CDC 已实现，默认 ON，编译/主机测试通过，未上板；见下方最新矩阵 |

## 采集缓冲池 ADR-013（2026-09-28，r12）

| 项 | 结果 | 证据 |
|---|---|---|
| VM301 ARM64 构建 worker+kvm | 通过 | worker 5979dbfe…，kvm 9d1735c6… |
| 离线测试 | 通过 | 117 项，2 跳过 |
| 开机池分配（T6 实机） | 通过 | 4×24,883,200 B，socket at:0600 |
| 碎片化后运行时分配 | 失败（预期） | 需重启，已记录 |
| worker 池化 1080p60 HEVC 实机采集+编码+独立解码 | 通过 | 175 帧，ffmpeg 无错，画面正常 |
| Windows 客户端连接 | 未测试 | 待用户实测 |

## RP2350 v0.2 控制通道 B（2026-09-28 最新；编译/主机测试，未上板）

| 检查 | 状态 | 证据与边界 |
|---|---|---|
| Docker 固件 ON / OFF | 通过 | text/bss 47904/19436 与 28424/6044 B；固定 SDK/PIO commit |
| Mac + Docker ASan/UBSan | 通过 | 核心 ON/OFF 各 200718 checks；CDC 描述符/请求、10万随机 setup、100万字节 SPSC 跨线程、通道仲裁/失效 |
| CDC EP0/bulk/ZLP 状态机 | 通过（模拟） | 生产 CDC 栈 + 控制器桩，非 USB 硬件 |
| 0.1 客户端协议兼容 | 通过（Mac 离线） | 原 Qt 12/12，未改客户端、未重新 Windows 构建 |
| Target UF2 交付与 SHA256 读回 | 通过 | results/20260928-rp2350-cdc/delivery.json |
| PIO CDC Windows/Linux 枚举、USB 时序、长稳/背压/Hub | 未测试 | 无真板 |
| 原生 HID、UART A/B 切换、拔线/DTR/挂起释放、WS2812 | 未测试 | 见固件 README 上板清单，先确认控制线断 VBUS |

完整产物/hash 与失败修复记录见 [本轮记录](../results/20260928-rp2350-cdc/STATUS.md)。没有 T6 操作，没有服务端或 client/ 改动，没有实机通过声明。
