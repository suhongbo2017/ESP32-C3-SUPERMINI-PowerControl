# PC Power Cloud

当前阶段提供设备一次性配对和设备凭据撤销基础。

## 本地运行

```bash
npm ci
npm test
PC_POWER_ADMIN_TOKEN="至少 32 个字符的随机值" PC_POWER_DATABASE="./data/pc-power.sqlite" npm start
```

默认监听 `127.0.0.1:3000`。生产环境应由 HTTPS 反向代理暴露，当前 Fastify 进程本身不提供 TLS。

## API

管理员接口使用配置的 Bearer token：

```text
POST /api/admin/pairing-codes
DELETE /api/admin/devices/:deviceId/credential
```

设备使用一次性配对码：

```text
POST /api/device/pair
```

配对码只在生成响应中返回，数据库只保存带盐哈希。配对成功后返回的设备凭据只应写入设备安全存储，不应交给浏览器或写入日志。配对码只能使用一次，设备凭据撤销后立即失效。

## 当前边界

- 尚未实现管理员账号登录、会话、登录限速和多用户权限。
- 尚未实现设备轮询、命令领取、命令回执和诊断事件 API。
- 尚未接入生产域名、Cloudflare Tunnel、备份和恢复演练。
- `sql.js` 用于避免开发机原生编译依赖；数据仍保存为 SQLite 文件。部署前需验证目标主机的文件权限、备份和并发性能。
