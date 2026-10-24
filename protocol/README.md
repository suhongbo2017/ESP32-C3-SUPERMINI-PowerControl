# 设备与云端协议

## 版本

当前协议版本为 `1`，所有设备请求和事件必须显式携带 `protocolVersion: 1`。

## 设计约束

- 设备只通过主动 HTTPS 访问云端。
- 命令只能是 `power_press`，不支持长按或强制关机。
- 每条命令使用唯一 `commandId`，并带 `issuedAt`/`expiresAt`。
- 设备端重复命令 ID 不得再次触发 GPIO 脉冲。
- Ping 只产生 `reachable`、`unreachable` 或 `unknown`，不得携带或推断 `pcPowerState`。
- 命令回执只表示脉冲执行结果，不表示电脑已经开机或关机。
- 关键字段必须校验；非关键未知字段可忽略，保持设备连接简单。

## 当前消息

### 配对请求

```json
{
  "protocolVersion": 1,
  "deviceId": "c3-001",
  "pairingCode": "一次性短时配对码"
}
```

### 设备轮询报告

设备通过 `POST /api/device/:deviceId/seen` 上报本地确定的诊断快照。云端只做协议校验、保存和展示，不读取设备 GPIO，也不重新探测 LED 或 Ping。

```json
{
  "protocolVersion": 1,
  "deviceId": "c3-001",
  "reportedAt": "2026-10-20T12:00:00.000Z",
  "deviceIp": "192.168.1.25",
  "hostIp": "192.168.1.10",
  "wifi": "connected",
  "ssid": "example-wifi",
  "reachability": "reachable",
  "sensorAvailable": true,
  "temperature": 35.2,
  "humidity": 48.1,
  "powerLedState": "low"
}
```

`reachability` 只能是 `reachable`、`unreachable` 或 `unknown`，由设备本地 Ping 得出；它不能代表电脑电源状态。`powerLedState` 只能是 `high`、`low` 或 `blinking`，由设备在 GPIO0 本地采样窗口内确定。当前硬件约定 `low` 为灯亮、`high` 为灯灭。云端页面只将这些协议值映射为中文文案。

### 云端下发命令

```json
{
  "commandId": "00000000-0000-4000-8000-000000000001",
  "kind": "power_press",
  "issuedAt": "2026-10-20T12:00:00.000Z",
  "expiresAt": "2026-10-20T12:00:15.000Z"
}
```

### 命令执行回执

```json
{
  "protocolVersion": 1,
  "commandId": "00000000-0000-4000-8000-000000000001",
  "status": "executed",
  "completedAt": "2026-10-20T12:00:00.300Z"
}
```

`status` 可为 `executed`、`failed` 或 `rejected`。失败时可增加脱敏的 `errorCode`。

### 可达性事件

```json
{
  "protocolVersion": 1,
  "eventId": "00000000-0000-4000-8000-000000000002",
  "kind": "reachability",
  "occurredAt": "2026-10-20T12:00:00.000Z",
  "reachability": "unknown"
}
```

## 进一步工作

HTTP 路由、认证头、配对响应、轮询领取响应、错误格式和日志事件完整 Schema 在后续认证/命令 tickets 中扩展；扩展必须保持上述安全语义。
