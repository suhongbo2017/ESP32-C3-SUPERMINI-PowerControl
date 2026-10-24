# 开发与运维

## 云端本地开发

```bash
cd cloud
npm ci
npm test
npm run build
```

本地运行需要设置环境变量：

```bash
PC_POWER_ADMIN_TOKEN='<随机长令牌>' \
PC_POWER_DATABASE='./data/pc-power.sqlite' \
npm start
```

生产环境应由反向代理或 Cloudflare Tunnel 提供公网 HTTPS；Fastify 不自行终止 TLS。

## 云端生产部署

生产部署使用 Docker：

- 服务容器：`pc-power-cloud-fastify`
- 数据目录：容器内 `/data`
- 配置文件：服务器上的 `service.env`
- Fastify 监听：`127.0.0.1:8310`
- 公网入口：Cloudflare Tunnel

部署必须保留现有 `/data` 数据卷，禁止用空目录覆盖数据库。启动后检查：

```bash
curl -fsS http://127.0.0.1:8310/health
curl -fsS https://<public-host>/health
```

部署前后都应确认设备配对数据、诊断数据和命令记录仍存在。管理员 Token 只从服务器配置文件读取，不写入镜像、源码或日志。

## 固件构建与增量烧录

固件使用 ESP-IDF v5.2.2，目标为 ESP32-C3、4 MB Flash。构建前确认 ESP-IDF 环境和目标芯片配置正确。

生产增量烧录只允许写应用分区，当前应用分区起始地址为 `0x20000`。不得执行全片擦除，不得修改：

- NVS
- bootloader
- 分区表
- eFuse

烧录后应通过串口确认：Wi-Fi 连接、云端 TLS 校验、设备 `/seen` 返回成功，以及没有新增栈溢出或重启。

## 现场验证

### LED

1. 观察实际 PLED 的亮、灭、闪烁状态。
2. 查看本地页面和云端页面的 LED 文案。
3. 确认 GPIO0 光耦隔离和输入极性。
4. 确认云端显示的是设备最近一次上报值，而不是云端自行测量值。

设备端状态变更需要经过本地采样窗口，云端页面还受 HTTPS 心跳间隔影响，因此显示可能短暂滞后。

### Ping

确认目标主机网络可达性时，只将 `reachable` 解释为网络连通；`unreachable` 不等同于电脑关机，可能由防火墙、休眠、网络切换或目标地址配置错误导致。

### 电源键

远程发送 `power_press` 前必须明确确认，因为命令会真实触发 GPIO1。验证顺序：

1. 检查设备在线。
2. 确认电脑当前状态和现场风险。
3. 发送一次命令，不重复点击。
4. 查看设备命令回执和串口日志。
5. 现场确认光耦动作及电脑实际反应。

云端回执 `executed` 只表示设备执行了 GPIO 脉冲，不表示电脑电源状态已经改变。

## 备份与恢复

备份 SQLite 数据库和 `service.env` 时必须使用受控权限。备份不得提交到 Git 或发送到聊天记录。恢复后应验证：

- 服务能够启动并通过 `/health`。
- 管理用户可以登录。
- 设备配对记录和 credential 哈希仍在。
- 设备能够继续上报 `/seen`。
- 命令和诊断历史可读取。

## 安全检查

禁止记录或提交：管理员 Token、设备 credential、配对码、密码、完整 Authorization 头和生产数据库。生产操作完成后清理临时 Cookie、命令响应和包含敏感配置的 shell 历史。
