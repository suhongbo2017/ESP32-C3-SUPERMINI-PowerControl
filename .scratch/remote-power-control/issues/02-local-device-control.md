# 02 — 实现设备本地控制核心

**交付内容：** 用户可以通过 ESP32-C3 的认证本地网页请求一次安全短按；设备同时提供 USB 维护日志、Wi-Fi/配置 AP 和可配置的 Ping 可达性报告。云端不可用不影响本地控制。

**阻塞项：** 01 — 建立项目骨架与设备协议

**状态：** in-progress

- [x] ESP-IDF 固件可在至少 4 MB 的 ESP32-C3 上构建
- [x] GPIO1 默认 LOW，执行器短按约 300 ms 后自动恢复 LOW
- [x] 启动路径先设置 GPIO1 为 LOW，再创建执行任务
- [x] 执行器忙时拒绝新命令且不排队；命令 ID 重复时拒绝重放
- [ ] 复位、OTA 和异常路径的实际板卡脉冲验证
- [ ] 本地网页要求独立本地凭据，凭据不依赖云端（认证已接入；USB 隐藏输入；NVS 加密待完成）
- [x] USB Serial JTAG 初始配置和维护命令已接入；日志可通过 USB 实时查看（待实板验证）
- [x] Wi-Fi 未配置或连续连接失败时提供开放配置 AP；AP 或 STA 断线时不提供电源控制 HTTP 服务（待实板验证）
- [x] Ping 目标可通过 USB 配置；周期探测仅报告 `reachable`/`unreachable`/`unknown`，不推断电脑电源状态（待实板验证）
- [ ] 云端不可达时本地网页仍能控制
- [ ] 设备侧测试和真实板卡冒烟测试完成（已添加控制门禁纯逻辑测试；当前环境缺少主机 C++ 编译器，尚未执行）
- 最近 ESP-IDF v5.2.2 构建：通过；应用镜像 `0xe18a0`，最小应用分区剩余约 51%。

## 当前验证记录

- ESP-IDF v5.2.2 `idf.py build`：通过。
- 4 MB 双 OTA 分区检查：通过；应用镜像 `0x56330`，最小应用分区剩余约 81%。
- 已实现 `PowerActuator`：GPIO1、300 ms 脉冲、忙态拒绝、内存命令 ID 去重、执行后强制 LOW。
- 已接入本地 HTTP server：`/api/local/status` 和 `/api/local/power-press`；仅在 STA 在线且本地凭据已配置时运行，STA 断开时停止；开放 AP 不提供控制入口。
- 已实现 HTTP Basic Auth 校验和 NVS 凭据加载；摘要为带 16-byte 随机 salt 的 10,000 次 PBKDF2-HMAC-SHA256。NVS 加密尚未启用；旧式无 salt 凭据不会被视为已配置，需重新设置。
- 已实现 USB Serial JTAG REPL：`local-status` 查看配置状态；`local-config` 通过隐藏 USB 输入设置本地凭据，密码不作为命令参数、不回显、不进入 REPL 历史，输入缓冲区使用后清零。已配置凭据目前需物理清除 NVS 才能重设。
- `wifi-config` 通过 USB 隐藏输入获取 SSID 和密码，敏感值不进入 REPL 命令历史；原始终端输入行为仍待实板验证。
- 当前未把本地凭据安全存储验收项标记为完成，因为还需要完成 NVS 加密配置。
- 已实现 `WifiManager`：Wi-Fi 凭据保存在 NVS；STA 最多重试 8 次后启动开放配置 AP；AP SSID 通过 USB 日志公布；USB 命令 `wifi-config` 隐藏输入配置家庭 Wi-Fi。
- 已实现 `ReachabilityMonitor`：USB 命令 `ping-target <ipv4-or-hostname>` 持久化 Ping 目标；在线时每 30 秒执行 3 次 ICMP 探测，未配置、无网络或 DNS 失败报告 `unknown`；结果通过 USB 诊断日志和认证后的 `/api/local/status` 返回。Ping session 在结束回调中请求销毁后再唤醒监控任务；目标代次与互斥锁共同保护目标快照和结果提交，阻止旧目标的迟到结果覆盖新状态。
- API 的 `pcPowerState` 固定保持 `unknown`；没有云端事件上传实现，本阶段不宣称已经云端上报。
- 配置 AP 是开放网络，不启动电源控制 HTTP 服务；本地控制仅在 STA 在线且独立凭据已配置时启动。当前尚无 AP 网页配网表单，配网凭据通过 USB 设置。
- 尚未实现：AP 网页配网表单、发布固件 NVS 加密和完整设备侧自动化测试；GPIO、认证、Wi-Fi/AP、隐藏输入及掉线行为仍待真实板卡验证。已新增本地控制门禁纯逻辑测试源码，但当前环境缺少主机 C++ 编译器，尚未执行。
- ESP32-C3 的 NVS 安全设计已记录在 `docs/security/nvs-encryption.md`：首选 HMAC eFuse key 保护 NVS，不强制启用整体 flash encryption；发布配置和首次 provisioning 会写入不可逆 eFuse，仍需独立评审与实板验收。
- Wi-Fi 事件状态和凭据 configured 标记已改为原子变量，避免跨任务无同步读写。
