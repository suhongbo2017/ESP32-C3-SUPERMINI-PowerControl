# PC Power Cloud

Node.js、TypeScript、Fastify 云端服务，负责设备配对、设备状态接收、命令生命周期、管理用户和管理页面。

## 开发

```bash
npm ci
npm test
npm run build
```

本地启动：

```bash
PC_POWER_ADMIN_TOKEN='<随机长令牌>' \
PC_POWER_DATABASE='./data/pc-power.sqlite' \
npm start
```

默认监听地址和端口以启动配置为准；生产环境通过 Docker 和 Cloudflare Tunnel 暴露 HTTPS，Fastify 本身不终止 TLS。

## 管理认证

管理页面支持两种登录方式：

- 管理员 Token：恢复入口和首次创建普通用户的入口。
- 用户名密码：密码使用 Node `scrypt` 加随机盐哈希保存。

浏览器登录后使用 HttpOnly、Secure、SameSite 会话 Cookie，并通过 CSRF Token 保护写操作。普通用户暂时拥有完整管理权限，没有只读角色。

用户名为 3-40 个 ASCII 字符，只允许字母、数字、`.`、`_`、`-`；密码长度为 12-256 个字符。

## 主要接口

管理接口需要有效管理会话：

```text
POST   /api/dashboard/login
POST   /api/dashboard/logout
GET    /api/dashboard/devices
GET    /api/dashboard/commands
GET    /api/dashboard/users
POST   /api/dashboard/users
PATCH  /api/dashboard/users/:username
DELETE /api/dashboard/users/:username
POST   /api/dashboard/devices/:deviceId/power-press
```

设备接口使用配对所得 credential：

```text
POST /api/device/pair
POST /api/device/:deviceId/seen
GET  /api/device/:deviceId/commands/next
POST /api/device/:deviceId/commands/result
```

管理员 Token 恢复入口和配对管理接口以 `server.ts` 的实际路由为准；不要把 Token 写入源码、测试固定值、日志或浏览器存储。

## 设备快照

`/seen` 中的诊断字段由设备本地采集和判断。服务端只验证并保存：

- `powerLedState`：设备 GPIO0 本地采样得到的 `high`、`low` 或 `blinking`。
- `reachability`：设备本地 Ping 得到的 `reachable`、`unreachable` 或 `unknown`。
- 网络信息、传感器读数和上报时间。

服务端不读取 GPIO，不执行 Ping，也不根据在线状态推断电脑电源状态。页面将 LED 显示为“灯亮/灯灭/灯闪烁”，将 Ping 显示为“通/不通/未知”。

## 命令语义

管理端创建的命令只能是 `power_press`。命令有效期约 15 秒，设备在线窗口约 45 秒，每次最多领取一条；不自动重试、不排队、不补发。回执只表示设备是否执行 GPIO1 的约 300 ms 脉冲，不表示电脑已经开机或关机。

## 数据和部署

SQLite 数据文件通过 `PC_POWER_DATABASE` 配置，生产数据应挂载独立卷并进行受控备份。生产部署、健康检查和回滚步骤见 [`../docs/operations.md`](../docs/operations.md)。
