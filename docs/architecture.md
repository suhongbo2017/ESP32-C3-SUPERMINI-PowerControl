# 系统架构

## 组件

```text
浏览器
  │ HTTPS + 会话 Cookie + CSRF
  ▼
Fastify 云端服务 ── SQLite
  ▲
  │ 设备主动 HTTPS
  ▼
ESP32-C3
  ├─ 本地 HTTP 网页 + Basic Auth
  ├─ GPIO1 电源键执行器
  ├─ GPIO0 PLED 本地采样器
  ├─ AHT25 温湿度传感器
  └─ 局域网 Ping 可达性检测
```

云端服务不向设备开放入站端口。设备通过 HTTPS 主动访问 `/seen`、`/commands/next` 和 `/commands/result`。公网 HTTPS 由 Cloudflare Tunnel 提供，Fastify 进程监听本机回环地址。

## 本地设备控制

本地网页和云端命令都调用同一个 `PowerActuator::request_press()`。执行器将 GPIO1 拉到约定有效电平约 300 ms；执行器忙时拒绝新命令，不排队、不重试。

设备启动、复位和 OTA 不得产生电源键脉冲。GPIO1 与电脑主板之间必须使用光耦隔离，不得直接连接主板开关信号。

## 诊断数据流

设备每次云端心跳前在本地采集诊断快照：

- 设备 IP、Ping 目标主机 IP、Wi-Fi 连接状态和 SSID。
- AHT25 温度和湿度；读取失败时标记传感器不可用。
- GPIO0 PLED 状态。
- 局域网 Ping 状态。

设备将快照作为 `/seen` 请求体中的 `powerLedState`、`reachability` 等字段发送。云端执行协议校验后，原样保存到 SQLite 的 `diagnostics_json`，管理页面仅做中文文案映射。

### LED 状态的权责边界

LED 状态完全由设备本地确定：

1. GPIO0 每 100 ms 采样一次。
2. 固件使用约 1 秒窗口统计电平和变化次数。
3. 变化次数达到阈值时上报 `blinking`，否则上报 `high` 或 `low`。
4. 云端只接受、保存和显示该状态，不读取 GPIO、不根据 Ping 或在线状态推断 LED 状态。

页面映射：

| 设备上报值 | 页面显示 |
| --- | --- |
| `low` | 灯亮 |
| `high` | 灯灭 |
| `blinking` | 灯闪烁 |
| 未知或缺失 | 未知 |

当前硬件约定为低电平亮、高电平灭。若现场光耦极性不同，应修改设备端极性解释或接线确认，不应在云端修正采样结果。

### Ping 状态的权责边界

Ping 也由设备本地执行并上报 `reachable`、`unreachable` 或 `unknown`。云端只展示：通、不通、未知。Ping 不能作为电脑开机、关机或睡眠状态的替代信号。

## 云端命令生命周期

1. 已认证用户请求 `power_press`。
2. 云端创建带唯一 ID、签发时间和约 15 秒有效期的命令。
3. 设备在线轮询时最多领取一条命令。
4. 设备端校验命令、去重并调用执行器。
5. 设备上报 `executed`、`failed` 或 `rejected` 回执。
6. 云端保存命令状态；不自动重试、不补发过期命令。

设备在线窗口约 45 秒。设备离线时，管理页面只能创建无法及时执行的短时命令，命令过期后不会在重连时补发。

## 认证与存储

- 本地网页使用独立的 HTTP Basic Auth。
- 云端管理页面使用 HTTPS、HttpOnly/Secure/SameSite 会话 Cookie 和 CSRF Token。
- 用户密码使用 Node `scrypt`、随机盐和常量时间比较；SQLite 不保存明文密码。
- 管理员 Token 保留为恢复和首次初始化入口。
- 设备 credential 只存储哈希副本于云端，设备侧保存于 NVS；撤销后必须重新配对。
- SQLite 通过仓储接口访问，数据库文件挂载到独立数据卷。
