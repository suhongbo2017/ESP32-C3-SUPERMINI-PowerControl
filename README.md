# ESP32-C3 PC Power Controller
成本十多元搭建远程开关机功能
ESP32-C3 电脑电源控制器，提供本地网页控制、云端 HTTPS 远程控制和设备诊断。
<img width="1594" height="1131" alt="image" src="https://github.com/user-attachments/assets/301f02eb-7002-4606-897d-82bbeb09122b" />

<img width="1200" height="929" alt="image" src="https://github.com/user-attachments/assets/9b2825a7-5067-4f8d-9173-3fa6100cef78" />


## 当前能力

- 本地网页：Wi-Fi 配置、本地账号、电源键短按、设备状态和诊断。
- 云端网页：用户名密码登录、设备在线状态、诊断信息、命令记录和远程电源键短按。
- 设备云端通信：设备主动通过 HTTPS 上报状态并轮询命令。
- 设备诊断：Wi-Fi、IP、SSID、AHT25 温湿度、PLED 状态和 Ping 可达性。
- OTA 基线：4 MB Flash 双应用分区，保留回滚能力。

## 重要语义

- GPIO1 只产生约 300 ms 的电源键短按脉冲。
- 命令回执只表示设备是否执行了脉冲，不表示电脑已经开机或关机。
- Ping 只表示目标主机网络是否可达，不能推断电脑电源状态。
- LED 状态由设备本地 GPIO0 采样和判定为 `high`、`low` 或 `blinking`，再通过 HTTPS 上报；云端不读取 GPIO，也不重新探测或推断 LED 状态。
- 当前接线约定为低电平表示灯亮、高电平表示灯灭；最终极性仍需结合现场光耦接线确认。

## 目录

- `firmware/`：ESP-IDF v5.2.2 固件。
- `cloud/`：Node.js、TypeScript、Fastify 云端服务。
- `protocol/`：设备与云端协议说明。
- `docs/architecture.md`：系统架构和数据流。
- `docs/wiring.md`：GPIO0/GPIO1、PLED/PWR_SW 光耦和 AHT25 接线说明。
- `docs/operations.md`：本地开发、测试、部署和现场验证。
- `docs/project-manual.md`：完整项目手册，包含引脚定义、接线、使用、端点、架构、部署、流程和开发进度。
- `docs/adr/`：重要技术决策记录。
- `reference/`：旧项目和原始资料，仅供调查，不作为当前实现规范。

## 快速验证

```bash
cd cloud
npm ci
npm test
npm run build
```

固件开发需要 ESP-IDF v5.2.2 环境。生产部署通过 Docker 和 Cloudflare Tunnel 完成，详细步骤见 [`docs/operations.md`](docs/operations.md)。

## 安全约束

凭据、管理员 Token、设备 credential、配对码和生产数据库不得提交到 Git、写入示例代码、日志或聊天记录。设备固件增量更新只写应用分区，不修改 NVS、bootloader、分区表或 eFuse。
