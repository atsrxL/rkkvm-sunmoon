# RKMoon 接手说明 — 2026-09-23

> 2026-09-25 最新：服务端发布 20260925-r10 已安装到 T6。无 HDMI 信号时，会话以 MPP 硬件编码的黑屏占位启动，键鼠照常可用（见 [ADR-011](docs/ADR-011-no-signal-session.md)）。worker 占位模式已在实机验证。客户端改为 Connect 后只显示并刷新服务端信息，按钮 View HDMI 改名为 Start，无信号时也能启动；这部分目前只通过了 Mac 离线测试，Windows 构建交付由子代理进行。

> EDID最新：用户最终8模式、首选1080p120已写入T6并读回一致；Znas RTX5060Ti通过Windows枚举切换，8模式全部接收锁定（含1440p120/1600p120/4K60）。仅输入时序验收，未做新增模式采集编码。测试后实际输出1080p60，EDID首选仍120Hz，未配置开机持久化。见 [EDID实测](docs/EDID-EIGHT-MODES.md)。

> 2026-09-24 最新状态：用户确认当前客户端在线且可用，保留现场会话。此前偶发断开/View HDMI失败仍未定位；T6持续约90fps并收到客户端心跳，RTSP重试出现tag校验失败。源码包含尚未构建交付的生命周期诊断与短暂metadata失败容错草稿，不代表故障已修复。用户要求上传全部最新源码；Target现用0fb281698fd2目录不随此次源码提交替换。

> 最新客户端0fb281698fd2已去掉嵌套common Connection.c的连接相对微移（前版导致绝对租约退出），全新Windows构建、Qt22/22。鼠标模式已移到主GUI。Target仅保留可运行目录RKMoon-Windows-x64，40文件读回校验，旧客户端ZIP/sidecar已清理，真实串流待用户复测。

> 最新交付要求：Windows客户端须以可直接运行的未打包目录交付Target，稳定路径RKMoon-Windows-x64；新目录校验完成后移除旧RKMoon客户端版本及ZIP/sidecar。其他项目和驱动保留。当前正在修复客户端底层连接微移导致绝对租约退出，并把鼠标模式移到主窗口。

> 双鼠标模式已交付：T6 `/home/at/rkmoon-mouse-modes/rkmoon-kvm` SHA b0fdc631，runtime allow_absolute_mouse=true，真实双USB鼠标，LAN发布relative,absolute。Windows mouse-modes-c6b7b7db715c（SHA e6fe614d）Qt22/22，默认绝对/可选相对触控板，C仅显示指针。非16:9映射离线通过、真实USB中心/象限坐标通过，客户端全链路待测。详见[鼠标模式记录](results/20260924-display/MOUSE-MODES.md)。

> 首帧退出已定位并修复：旧auto-display-1b610d1包的SDL自定义停止事件与上游FRAME_READY撞号，应停用。新Windows auto-display-exitfix-b49e86cccbc3已交付Target，SHA e9b6e076，Qt18/18与冷启动事件回归通过，真实用户复测待确认。见[缺陷与修复记录](results/20260924-1440p90/DISPLAY-FOLLOW.md)。

> 自动画面模式服务端已部署：`/home/at/rkmoon-display-follow/rkmoon-kvm`，SHA47cd6a1f，原身份/worker/input配置保留，两个服务active，LAN认证后发布2560×1440 fps_x1008999。Windows auto-display-1b610d1bf94a已交付Target，SHA475e71cd，Qt17/17及启动检查通过；异步跟随与本地指针代码已交付，真实现场切换/指针仍未验收。视频设置仅码率/编码。证据见 [本轮记录](results/20260924-1440p90/DISPLAY-FOLLOW.md)。

> 当前源已按用户选择切到2560×1440 90Hz：Windows NVAPI自定义保存成功，客体90Hz，T6实测89.9935Hz；保留当前90Hz EDID，未设置开机EDID恢复。input.enabled=true，USB枚举OK，两个服务active。不要再称当前源1080p；见[客户端复查](results/20260924-1440p90/CLIENT-FOLLOWUP.md)。

> 客户端后续：用户确认1080p可见，2K日志明确source/client尺寸不匹配，实际源仍1080p60。Znas USB直通已验证，Windows识别RKMOON001键鼠OK；T6后端残留offline，经只重启rkmoon-input恢复online，runtime input.enabled已恢复true，专用服务重启。见[复查](results/20260924-1440p90/CLIENT-FOLLOWUP.md)。以下input禁用描述为历史。

> 当前T6已切HTTP密码实验server：/home/at/rkmoon-http-1440p90，rkmoon-http-live active（临时unit、at用户），LAN正确密码serverinfo/applist200、password-http-v1、HttpsPort0、错误密码401。旧912613c服务停止但目录保留。USB键鼠硬件报告offline，故新runtime input.enabled=false；视频源仍1080p60。见[部署记录](results/20260924-http-password/T6-DEPLOY.md)。以下未部署描述为历史。

> 后续实机编码通过：用户纠正后重读sshh，T6登录恢复；HEVC/H2641440p90各60s编码和独立完整解码通过（5394帧，各89.998/89.982fps，DMA-BUF，0/1raw skip，P95 7.025/6.672ms）。原EDID/1080p60已恢复，测试worker释放，原服务保持。见 [最新编码证据](results/20260924-1440p90/ENCODE.md)。以下“SSH阻碍/未编码”为历史，不再是T6当前状态；Windows子代理正按更新sshh重读正确凭据续建，端到端90fps尚未测。

> 实验90Hz服务端构建完成：worker/server ARM64完整链接，30原生通过、91/93 Python联合通过（2条件跳过）、配置gate15项通过。源码默认仍60Hz；开启allow_high_resolution与allow_1440p90_experiment后才允许2560×1440约90Hz，worker显式CLI开关，GOP90、HEVC5.1/H2645.2。产物/hash见 [构建记录](results/20260924-1440p90/SERVER-BUILD.md)，尚未部署server或完成90fps实机编码。

> 1440p90采集阶段更新：真实V4L2完整帧30s通过（2695帧，timestamp89.9981fps，零序号缺失/错误，每帧11059200字节），见 [采集记录](results/20260924-1440p90/CAPTURE.md)。实验worker9158a60已上传独立目录并验证hash/依赖；下一次SSH认证失败，编码未执行。原EDID和1080p60已恢复、NV临时任务已清理。client实验预设Mac Qt12/12通过，Windows构建因MS-A2 SSH认证失败未执行；不要把旧Windows包当作新包。

> NVIDIA后续已解决：NVAPI自定义1440p90两次成功，T6两次实测2560×1440、89.9935Hz；成功EDID与时序见 [NVIDIA记录](results/20260924-1440p90/NVIDIA.md)。下条失败记录为历史。试用均自动恢复，未保存Windows自定义模式，原EDID已恢复。下一步是真实采集/MPP吞吐与server/client90Hz实现，尚未完成。

> 1440p90最新实测：当前源为Znas Tiny11_clone直通RTX5060Ti，不再是NUC9。临时EDID加入90Hz并调整TMDS上限后，Windows仍拒绝模式（CDS_TEST=-2）；原EDID已读回确认恢复，T6恢复1080p59.9952、两个rkmoon服务active。未放宽90Hz代码、未执行90Hz采集/编码。见 [试验及回滚](results/20260924-1440p90/STATUS.md)。

> 当前HTTP版服务端：ARM64 Linux完整链接、实际HTTP密码认证/无HTTPS监听/无PEM生成/重启身份回归通过；详见 results/20260924-http-password/STATUS.md。Windows客户端由Cpad/gpt-6-sol medium子代理接续构建交付，原Claude代理额度已耗尽。未访问T6。

> 最新要求：HTTP＋密码，取消HTTPS/证书验证；默认密码 `kvm`，Windows为主要客户端。此条取代下文旧TLS/TOFU要求，见 [ADR-005](docs/ADR-005-http-password.md)。

> 密码认证 P0：2026-09-24 ARM64 Linux完整链接、非root真实HTTPS认证/旧pair关闭/重启身份稳定测试通过（模拟probe，无媒体流）。28原生及91/93工具测试通过、2缺内核fixture跳过。见 [新记录](results/20260924-password/STATUS.md)。T6部署和真实新客户端流未测试。

> 2026-09-24：按用户要求改为 IP＋密码（初版 `kvm`），取消 PIN/pair 和客户端证书登记授权。TLS/输入加密保留；旧配对说明及旧实机通过记录不代表新认证验收。见 [认证决策](docs/ADR-004-password-auth.md)。新版本尚未部署到设备。

> HDMI音频专项 `fix/hdmirx-audio-codec`：固定95e85f6确认TX禁capture回归误伤RX，精确26.5.1 headers/config/symvers构建RX-only独立模块已交付（[源码与证据](kernel/hdmirx-codec/README.md)）。父会话已验证SHA、加载并仅重绑codec.5，card0 capture PCM恢复；真实48k stereo S16录音仍报Input/output error，**声音未通过**。本子会话仅编译/源码分析，硬件与回滚由父会话掌控。

> 最小服务分支 `feat/minimal-kvm-server` 在独立目录开发，**不代表下述旧 main 验收已迁移**。已改为专用 `rkmoon-kvm` 无 WebUI/npm 主入口，保留 Sunshine GameStream 配对/RTSP/RTP/FEC/认证加密输入、固定 HDMI 应用；本地 0700/0600 Unix PIN CLI；显式 ALSA HDMI stereo 48k→Opus，缺源实时静音，断开重试。冻结912613c已完成ARM64 QEMU完整链接、25原生+87工具/联合、无Web模拟启动；runtime及精确hash见 `results/20260923-minimal/STATUS.md`，已交父会话，不包含后续3da8050启动文案。生产音频object的null/xrun合成测试通过；真实硬件/客户端音画仍未测，T6 HDMI card无PCM节点由父会话调查。构建 VM301 是 x86_64 客体跑 arm64 QEMU 容器，不是 T6 原生测试。详见 `docs/ADR-002-minimal-service.md`；不要在旧 VNC 项目部署/覆盖。

> 实机更新：ARM64 worker/Sunshine 已构建；T6 真实 1080p60 HEVC/H264 各 60 秒及独立解码通过；原版 Moonlight HEVC 硬解超过 11 分钟、H264 重连通过。USB 尚未完成。见 [T6 验收记录](results/20260923-t6/STATUS.md)。

> 2026-09-23 本地更新：完整 Sunshine 与真实 MPP worker 已在 Linux amd64 编译链接通过；25 原生、69 Python/联合、25 sanitizer 测试通过。目标 ARM64/P1/P2/P3 仍未测试。最新结果见 [构建记录](results/20260923-build/STATUS.md)。下文原交接记录保留供追溯。

## 用户目标和已做决策

独立 NanoPC-T6 / RK3588 HDMI KVM，原版 Moonlight、HEVC Main8 硬编码优先、H264 硬编码回退、USB 输入，而非 T6 本机 uinput。用户不要求复用旧 VNC 架构。保留旧方案作为回滚，不擅自停旧服务。

当前决策 B：独立 V4L2/MPP worker + 专用 Sunshine 薄适配层 + 独立输入租约服务调用既有 kvmd。控制 IPC 自定义但仅为同机私有协议；**GameStream 网络协议没有重写**。

## 固定源码

| 名称 | Commit | 用途 |
|---|---|---|
| Sunshine v2026.914.233613 | `63d35f702ee9e362e43263742981836ec0710384` | 配对/会话/加密/RTP-FEC 与平台输入入口 |
| Rockchip MPP | `0986d01294d5c2449c14cf13af9b740368c33967` | 编码 SDK/runtime |
| nanopc-t6-kvm | `6c381b30f8fc3b34a70a63840eb817f549e844b2` | 底层设计与旧实测参考，不作为依赖安装 |
| kvmd v4.120 | `78ff181e95b14327831441d58f2f7f4cb2181cde` | 本包核对的 HID API；实机版本必须另记 |

机器可读清单是 `sources.lock.json`。Sunshine 上游 C++23；本项目独立 worker C++17。不要误以为同一编译器最低版本完全相同。

## 代码导览

`src/capture.cpp`：MPLANE 的单 plane 原生 NV12/BGR24、timings/layout 校验、4 个 MMAP buffer、可选 EXPBUF、仅丢未编码 raw、源变化终止。不写 S_FMT/EDID。

`src/encoder_mpp.cpp`：真实 MPP API、Main8 HEVC/Baseline H264、明确导入或拷贝路径、CBR/GOP/强制 IDR、完整 AU 和 PTS 校验。**尚未对真实 SDK 编译链接**。最先完成这一门。

`src/core.cpp` / `annexb.cpp`：64 字节版本化 IPC 头、有限 AU、绝对 I/O deadline、序列校验、首帧/恢复 IDR 与 inline 参数集检查；不是完整 H264/HEVC 语法解析器。

`src/worker_main.cpp`：独立码流测试、CSV 与统计、MPP 合成黑帧能力探测、受控子进程。普通采集无合成画面和软件编码兜底。

`sunshine/` + `tools/apply_sunshine.py`：固定 commit 的补丁生成器，修改 7 个上游文件，再增加自有辅助源码。**不含对完整上游树实际套用/编译通过的声明**。默认 dry-run，锚点缺失/重复及脏树拒绝。小型锚点测试已通过，不等同完整套补丁通过。

`src/hid_client.cpp` + `python/rkmoon_hid/`：SO_PEERCRED、私有 UDS、单租约、有界键鼠队列、心跳、失败阻断与 release retry。kvmd 接受事件不等于 USB host 已消费。

`tools/run.py`：专用二进制 marker 检查、独立 HOME/XDG/state、前台监督、pidfd 安全停止、子进程 parent-death SIGTERM、停止后单独 release-all。`systemd/` 只是模板，未安装未 enable。

## 本次证据

云端 x86_64 Linux，无 RK3588/MPP SDK、无 Moonlight 客户端。25 原生测试、68 Python/联合/工具测试通过；ASan/UBSan 25 原生用例通过；worker CLI 独立编译通过。见 `results/cloud/`，记录有实际命令/日志。编译容器无法解析 GitHub，GitHub 连接器可读公开源码，故完成了源码分析而没有完整源码 checkout 构建。

## 最高优先级风险

1. **全量构建门未过。** 核对实际 MPP API/配置项、包 PTS、EOI、import 缓存/同步和错误清理；固定 Sunshine 的所有锚点/类型/目标链接。
2. **1080p 对齐。** 直接路径要求 width%64=0、height%16=0、精确 stride/sizeimage。本版1080p需要显式 `--allow-copy`。不能悄悄放宽布局、借用超出 buffer 的高度或改源模式。
3. **输入布局。** 旧记录 mouse.absolute=true；本实现拒绝。不能仅把相对 delta 写给绝对 HID。先视频，后在授权窗口解决 descriptor/kvmd 相对实例。
4. **客户端/无音频。** 原版配对、HEVC 硬解、H264 fallback、无音频可连接、重复连接、恢复控制都需要真实客户端验证。
5. **低延迟不是承诺。** 发送入口 budget 200ms 是停止积压阈值，不是目标延迟；不包含 HDMI dequeue 之前和客户端显示。kvmd HTTP 逐事件转发的延迟/队列必须实测。
6. **部分功能仅控制原语。** worker 具备降码率指令，Sunshine 自动动态码率链路未接；不能宣称自适应码率完成。
7. **输入最后一公里。** 断开后的逻辑 release/retry 与 SIGKILL 联合测试通过，不代表 USB 物理断开或 kvmd 卡死时能够绝对保证目标已释放；异常必须锁住接管权并报告。

## 下一步顺序

执行 CODEX_START.md；不再扩展功能优先级。先全量构建、P1 的真实 1080p60 HEVC/H264，再 P2 video-only，最后 P3。每项未测试保持未测试，不拿旧日志替代。

## 通用源模式部署（2026-09-24）

T6 已运行 ADR-009 通用模式服务端：/home/at/rkmoon-custom-modes/rkmoon-kvm（b3819478…）+ worker（18164eaa…，拷贝路径去掉整缓冲 memset）。
实机扫测见 results/20260924-general-modes/HARDWARE-SWEEP.md：除 1080p120（CPU 拷贝约108fps）外，8个EDID模式采集+硬编+独立解码均通过。
EDID 未做开机持久化；T6 重启后需重新写入。Znas 上临时计划任务 RKMoon-Select-Mode 已清理，当前源为 1080p120。

## 输入/断线缓冲部署（2026-09-24，ADR-010）

T6 当前运行 /home/at/rkmoon-r3/（rkmoon-kvm 8c7cbf8a…，worker 647c109e…），HID Python 已更新到 /home/at/rkmoon-http-1440p90/python/rkmoon_hid（WebSocket 事件流）。回滚：/root/agent.backup/rkmoon-r3-20260924/ 下的 runtime.json 和 rkmoon_hid，旧二进制仍在 /home/at/rkmoon-custom-modes/。客户端实测未完成。

## 正式安装布局（2026-09-24 晚）

T6 服务端已从 home 下的散落目录迁到 /opt/rkmoon/releases/20260924-r9（current 链接），配置 /etc/rkmoon/runtime.json，状态 /var/lib/rkmoon。rkmoon、rkmoon-edid、rkmoon-hdmirx-audio、rkmoon-input 全部开机自启，EDID 开机自动写入。打包/安装/回滚见 docs/SERVER-INSTALL.md。旧临时服务 rkmoon-http-live 已停止；旧运行目录（/home/at/rkmoon-*、/usr/local/lib/rkmoon、/opt/rkmoon-input-venv、/opt/rkmoon-kvmd-source 及 /root/agent.backup 下的旧 rkmoon 备份）已于同日删除，约 2GB；只保留原始 EDID 备份。回滚只能用 /opt/rkmoon/releases 里的发布。HID 桥在 USB 离线时不再退出，而是拒绝租约直到按键释放成功。尚未做整机重启验证。

## RP2350 HID 桥 Windows 测试客户端（2026-09-28，ADR-012）

tools/rp2350-client/：Qt 6.8.3 Widgets + QSerialPort，直接通过串口使用板子协议 v1（HELLO/INFO、200 ms PING/PONG、捕获区键鼠、RELEASE_ALL、"rkmoon" 输入测试）。协议/CRC 在 src/rp2350_protocol.*，不依赖界面。共享测试向量 docs/rp2350-protocol-vectors.json（本任务先定义，固件应复用）。ADR-012 补充了：PIO-USB 引脚 GP12=D+、GP13=D-（官方原理图与示例），两个 Type-C 口 VBUS 直连 VSYS 的供电警告，精确的重新同步规则、长度/seq/buttons 约定，以及 CRC 错误时的 NAK 不能用来重发。
离线结果：Mac Qt 6.11.2 核心测试 12/12；Windows VM9007 Qt 6.8.3/VS2022 完整 QtTest 17/17，打包后 4 秒干净 PATH 启动冒烟通过。交付 smb://192.168.123.10/zssd/Target/RKMoon-RP2350-Client，30 个文件，delivery-manifest.json SHA256 682caedf8e191d019d8fb78ed18799ac7deb3fc475e642142c373b63ba7eeb9b，Mac 与 Windows UNC 两侧逐文件读回一致。
**未接真板子**：真实 COM 口、HID 枚举、被控机输入和看门狗均未测试。VM9007（手工 ZFS 链接克隆 env-admin-ssh）已停机，清理截止 2026-09-29 15:05:22 CST。

## RP2350 HID 桥固件（2026-09-28，ADR-012）

firmware/rp2350-hid/：pico-sdk 2.3.1 + 自带 TinyUSB 0.18.0，复合 HID（boot 键盘、相对鼠标 int16、绝对指针，remote wakeup），UART0 GP0/GP1 1 Mbaud 协议 v1，500 ms 看门狗全释放，相对移动拆分/合并，WS2812 灯。协议、CRC、看门狗和报告队列在 src/core（与硬件无关）。编译用 `firmware/rp2350-hid/tools/build.sh`（Docker，依赖 commit 见 deps.lock）。主机测试 200712 项 0 失败（含共享向量）；固件仅编译验证，**未上板**。uf2 SHA256 351562e62cef259720ef57986ffc82f80e8240e176c7db1c388fb7354785da31，已交付 Target/RKMoon-RP2350-HID 并读回一致。PIO-USB CDC 通道未实现（TinyUSB 单设备栈，原因见固件 README）。ADR-012 补充了 NAK 原因 5（版本）、6（未建立会话）、挂起时的处理和队列细节，Windows 客户端对未知原因码只显示数字即可。

## 采集缓冲池（2026-09-28，ADR-013，r12）

根因：T6 运行 4 天后 CMA 碎片化，worker 每次会话 REQBUFS(MMAP) 申请 6 MB 连续内存失败（dmesg cma_alloc ret -16），客户端表现为画面一闪即断。修复：新增 rkmoon-capture-pool.service（root，仅分配 dma-buf 并经 SCM_RIGHTS 借给 at），开机分配 4×24,883,200 字节并常驻；worker `--capture-pool` 以 V4L2 DMABUF 入队，CPU 读取前后做 DMA_BUF_IOCTL_SYNC；池不可用时回退 MMAP 并记日志；worker 失败原因经 IPC 进入 sunshine.log。未改 CMA 大小、内核或引导参数。
发布 20260928-r12（tar SHA256 c7e668f75b9be7775e9013e5e3c3432da9e74a5284991e52fba277205fcd645c，worker 5979dbfe…，kvm 9d1735c6…）。r11 为同源但缺 cache sync 的中间版本，不要激活。
实机：碎片化状态下池分配 5 次均失败（证实运行时无法恢复），重启 T6 后开机即 ready，CmaAllocated≈106 MB；独立 worker 1080p60 HEVC 3 s 走池化路径（capture_pool buffers=4），175 帧，Mac ffmpeg 解码无错，画面正常。离线测试 117 项通过（2 跳过）。**Windows 客户端连接由用户实测待确认。**
注意：rkmoon-capture-pool 不要随意 restart；若失败只能重启 T6。
