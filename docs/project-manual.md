# ESP32-C3 PC Power Controller 项目手册

**版本**：当前代码基线

**适用对象**：开发、部署、现场接线、运维和后续维护人员。

**项目目录**：`D:/PI/ESP32-C3-PC-POWER-NEXT`

本文以当前仓库代码为准。代码已确认的事实与必须现场确认的事项分开描述；未确认的硬件型号、排针位置、电阻值和主板极性不能根据本文自行假设。

## 1. 项目目标与边界

本项目使用 ESP32-C3 控制电脑主板的电源键，并采集电脑电源 LED、网络可达性和环境温湿度，通过本地网页和云端网页提供管理能力。

核心边界：

- GPIO1 只模拟一次电源键短按，当前有效脉冲为 HIGH 约 300 ms。
- 云端命令执行回执只代表 GPIO1 脉冲已执行，不代表电脑已经开机、关机或进入睡眠。
- Ping 只表示目标网络可达性，`unreachable` 不能推断电脑关机。
- ESP32 与电脑主板的 PLED、PWR_SW 必须光耦隔离，不能直接共接主板信号。
- 云端采用设备主动 HTTPS 轮询，不开放设备入站端口。
- 云端命令不自动重试、不排队、不补发；命令约 15 秒过期，设备在线判定窗口约 45 秒。
- 当前固件有双 OTA 应用分区，但尚未实现 OTA 下载/写入接口，因此当前升级方式仍是串口写入应用分区。

## 2. 目录与职责

```text
firmware/                 ESP-IDF v5.2.2 固件
firmware/main/            设备业务模块
firmware/partitions-4mb.csv 4 MB Flash 分区表
cloud/                    Node.js + TypeScript + Fastify 云端服务
cloud/src/server.ts       HTTP API 与内嵌管理页面
cloud/src/pairing/        配对、设备、用户、SQLite 仓储
cloud/src/commands/       命令创建、领取、回执、过期
cloud/src/protocol/       请求协议校验
protocol/                 设备云端协议说明
docs/architecture.md      简版系统架构
docs/wiring.md            接线和现场检查
docs/operations.md        运维和部署
docs/adr/                 技术决策记录
reference/                旧项目和历史资料，不作为当前实现规范
```

主要固件模块：

| 模块 | 文件 | 职责 |
|---|---|---|
| 启动编排 | `main.cpp` | 初始化 NVS、执行器、传感器、Wi-Fi、Ping、云端和本地 HTTP |
| Wi-Fi | `wifi_manager.cpp/.hpp` | 读取 NVS、连接 STA、失败时启动配置 AP |
| 电源执行器 | `power_actuator.cpp/.hpp` | GPIO1 安全初始化、300 ms 脉冲、忙碌和重复命令保护 |
| PLED 监测 | `power_led_monitor.cpp/.hpp` | GPIO0 每 100 ms 采样并识别稳定/闪烁 |
| 环境传感器 | `environment_sensor.cpp/.hpp` | AHT25 I2C 读数，10 秒更新一次 |
| 可达性 | `reachability_monitor.cpp/.hpp` | 每 30 秒执行 3 次 IPv4 Ping |
| 本地服务 | `local_server.cpp/.hpp` | Basic Auth、网页、设置和本地控制 API |
| 云端客户端 | `cloud_client.cpp/.hpp` | HTTPS 配对、心跳、诊断上报、命令轮询和回执 |
| 维护控制台 | `maintenance_console.cpp/.hpp` | USB 串口维护命令 |

## 3. 固件硬件定义

### 3.1 引脚总表

| ESP32-C3 GPIO | 方向 | 固件定义 | 用途 | 电气要求 |
|---|---:|---|---|---|
| GPIO0 | 输入 | `PowerLedMonitor::kGpio` | PLED 光耦输出采样 | 内部上拉；只能接隔离后的逻辑输出 |
| GPIO1 | 输出 | `PowerActuator::kGpio` | PWR_SW 光耦驱动 | 空闲 LOW，触发 HIGH 约 300 ms；不能直连主板 |
| GPIO4 | I2C SDA | `EnvironmentSensor::kSda` | AHT25 数据 | 3.3 V 逻辑，适当上拉 |
| GPIO5 | I2C SCL | `EnvironmentSensor::kScl` | AHT25 时钟 | 3.3 V 逻辑，适当上拉 |
| 3V3 | 电源 | 板级电源 | AHT25 和 ESP32 侧光耦逻辑 | 不得给主板信号侧供电 |
| GND | 地 | ESP32 本侧地 | 传感器和本侧光耦公共地 | 不与主板 PWR_SW/PLED 侧直接共地 |

GPIO0/GPIO1 的物理排针位置取决于具体开发板，必须按该板丝印和原理图确认。GPIO0 可能具有启动相关功能，现场必须确认外围电路不会影响启动。

### 3.2 电源键时序

启动阶段先把 GPIO1 配置为输出并设置 LOW，再创建执行任务。收到有效命令后：

```text
GPIO1 = LOW  ->  GPIO1 = HIGH 约 300 ms  ->  GPIO1 = LOW
```

执行器队列容量为 1；忙碌时返回 busy；同一命令 ID 已执行过时返回 duplicate。系统不排队、不自动重试。

### 3.3 PLED 采样算法

GPIO0 配置为输入并开启内部上拉。每次采样窗口包含 10 个采样点，间隔 100 ms，约 1 秒；窗口内变化次数达到 2 次判为 `blinking`，否则按多数电平判为 `high` 或 `low`。

当前典型光耦接法是：集电极接 GPIO0、发射极接 ESP32 GND、GPIO0 上拉到 3.3 V。光耦导通时 GPIO0 被拉低。软件上报的是原始 GPIO 电平：

| 原始值 | 云端协议值 | 当前页面文案 |
|---|---|---|
| 低 | `low` | 灯亮 |
| 高 | `high` | 灯灭 |
| 变化 | `blinking` | 灯闪烁 |

最终物理亮灭极性必须现场确认，不能只依据云端文案。

### 3.4 AHT25 定义

代码已确认：

- I2C 控制器：`I2C_NUM_0`
- SDA：GPIO4
- SCL：GPIO5
- 7-bit 地址：`0x38`
- 总线速度：100 kHz
- 固件启用 ESP32 内部上拉
- 初始化命令：`BE 08 00`
- 读取间隔：10 秒
- 读取失败时 `sensorAvailable=false`，温湿度为 `null` 上报

模块若已有外部上拉，应检查是否与内部上拉并联过强；传感器只使用 3.3 V 逻辑。

## 4. 接线方法

### 4.1 总体连接

```text
电脑主板 PLED +/-  -- 光耦输入侧 -->  ESP32 GPIO0
ESP32 GPIO1        -- 光耦输入侧 -->  光耦输出侧 -- 干接点 --> 主板 PWR_SW 两针
ESP32 3V3/GND      --> AHT25 和 ESP32 侧光耦电路
ESP32 GPIO4/GPIO5 --> AHT25 SDA/SCL
```

### 4.2 PLED

主板 PLED 信号进入光耦 LED 输入侧，必须串联限流电阻，电阻按实际主板电压、光耦正向压降和目标电流计算。光耦输出侧连接 ESP32 3V3、GPIO0 和 GND，不能将主板 PLED 电压直接接入 GPIO0。

接线检查：

1. 确认输入侧不会明显影响主板 PLED 亮度。
2. 确认输出侧高电平不超过 ESP32 允许范围。
3. 分别让电脑 LED 稳定亮、稳定灭和闪烁，比较本地页面、串口和云端文案。

### 4.3 PWR_SW

GPIO1 通过限流电阻驱动输出光耦；光耦输出侧作为无源干接点并联在主板 PWR_SW 两端。保留原机箱电源按钮。不得把 ESP32 GND 或 GPIO1 电压接到主板 PWR_SW。

GPIO1=LOW 时必须保证光耦截止；复位、高阻和上电瞬间不得误导通。首次测试先断开主板 PWR_SW 输出侧，只测光耦动作和约 300 ms 脉冲，再接入主板。

光耦型号、CTR、隔离耐压、输入电阻、输出器件类型和主板接口极性目前不是代码可确认事实，必须现场记录。

### 4.4 AHT25

```text
ESP32 3V3 -> AHT25 VCC
ESP32 GND -> AHT25 GND
GPIO4     -> AHT25 SDA
GPIO5     -> AHT25 SCL
```

传感器模块和 ESP32 本侧共地；主板隔离信号侧不与该地直接相连。

## 5. 本地设备使用

### 5.1 首次启动

设备启动后先读取 NVS 中的 Wi-Fi 配置。如果没有有效配置或连接失败，固件启动配置 AP。AP 的具体 SSID 由设备 MAC 派生，当前代码会记录类似 `PC-POWER-xxxx` 的设备专用 SSID，现场以串口日志为准。

通过配置页面提交 Wi-Fi 后，设备保存到 NVS 并尝试重新连接。连接成功后启动本地 HTTP 服务。

### 5.2 本地页面

默认中文首页：`http://<设备IP>/`

英文首页：`http://<设备IP>/?lang=en`

设置页：`http://<设备IP>/settings`

首页包含设备状态、Wi-Fi/IP、Ping、PLED、云端状态、温湿度曲线和电源键控制。英文页面只改变显示文案，不改变 API 和控制逻辑。

本地服务使用 HTTP Basic Auth。密码由设备本地认证模块保存和校验，不依赖云端账户。

### 5.3 本地端点

| 方法 | 路径 | 认证 | 作用 |
|---|---|---|---|
| GET | `/` | Basic Auth | 本地控制首页 |
| GET | `/settings` | Basic Auth | Wi-Fi、密码和云端配对页面 |
| GET | `/api/local/status` | Basic Auth | 返回 Wi-Fi、IP、Ping、温湿度、PLED、云端状态 |
| POST | `/api/local/configure` | Basic Auth | 兼容的本地配置入口，使用表单数据 |
| POST | `/api/local/password` | Basic Auth | 更新本地登录密码 |
| POST | `/api/local/wifi` | Basic Auth | 保存 Wi-Fi 并重新连接 |
| POST | `/api/local/cloud` | Basic Auth | 使用 HTTPS 地址、设备 ID、一次性配对码配对 |
| POST | `/api/local/ping-target` | Basic Auth | 保存 Ping 目标到 `ping_config.target` |
| POST | `/api/local/power-press` | Basic Auth | 真实触发一次 GPIO1 电源键脉冲 |

本地电源按钮必须二次确认。所有表单提交都不得在日志、浏览器历史或聊天记录中保存密码、配对码或 credential。

### 5.4 Ping

目标写入 NVS namespace `ping_config` 的 key `target`。设备每 30 秒执行一次 IPv4 Ping，会发送 3 个探测包，每包间隔 500 ms、超时 1 秒。状态值：

- `reachable`：至少收到回复。
- `unreachable`：解析成功但 3 次均无回复。
- `unknown`：未配置目标、网络不可用、DNS 解析失败或 Ping 会话启动失败。

## 6. 云端配对与运行

### 6.1 配对流程

1. 管理员通过恢复 Token 调用配对码接口。
2. 云端生成一次性配对码，有效期约 10 分钟；数据库只保存哈希。
3. 本地 `/settings` 输入云端 HTTPS 地址、设备 ID和配对码。
4. 设备 POST `/api/device/pair`。
5. 云端返回设备 credential；云端保存 credential 哈希，设备将明文 credential 写入 NVS `cloud_config`。
6. 设备每 5 秒执行一次云端轮询。

设备 ID 当前使用小写字母、数字和短横线，长度 3-64；生产设备为 `pc-power-c2d4`。设备 URL 必须以 `https://` 开头。

### 6.2 设备云端轮询

每 5 秒一次，且要求 Wi-Fi STA 已连接：

1. POST `/api/device/<deviceId>/seen`，Bearer credential，携带完整诊断快照。
2. GET `/api/device/<deviceId>/commands/next`，Bearer credential。
3. 若领取到 `kind=power_press`，校验命令 ID并调用 GPIO1。
4. 等待脉冲完成后 POST `/api/device/<deviceId>/commands/result`。

TLS 使用 ESP-IDF certificate bundle 校验证书。云端任务栈为 12288 字节，以容纳 TLS 握手开销。系统时间通过 SNTP `pool.ntp.org` 同步，用于 HTTPS 时间和命令回执。

### 6.3 云端管理认证

管理页面：`https://pc.19831018.xyz/`

支持两种登录方式：

- 管理员 Token：恢复和首次初始化入口。
- 用户名密码：密码使用 Node `scrypt` 加随机盐哈希；数据库不保存明文。

登录后使用 HttpOnly、Secure、SameSite=Strict 会话 Cookie，并要求 CSRF Token。当前所有已登录用户都是完整管理权限，没有只读角色。

## 7. 云端端点与原理

### 7.1 公共/健康端点

| 方法 | 路径 | 认证 | 说明 |
|---|---|---|---|
| GET | `/` | 无 | 返回内嵌管理页面 |
| GET | `/health` | 无 | 返回 `{ "status": "ok" }` |
| POST | `/api/device/pair` | 无，需有效配对码 | 一次性配对并签发 credential |

### 7.2 设备端点

| 方法 | 路径 | 认证 | 说明 |
|---|---|---|---|
| POST | `/api/device/:deviceId/seen` | Bearer device credential | 更新在线时间和诊断快照 |
| GET | `/api/device/:deviceId/commands/next` | Bearer device credential | 原子领取一条未过期 queued 命令 |
| POST | `/api/device/:deviceId/commands/result` | Bearer device credential | 提交 executed/failed/rejected 回执 |

设备 credential 只用于对应设备，撤销后上述端点返回 401。`seen` 只接受协议版本 1、设备 ID、时间、IP、Wi-Fi、SSID、传感器、PLED 和 Ping 等字段；云端不重新探测设备状态。

### 7.3 管理端点

这些端点要求登录会话和 CSRF Token：

| 方法 | 路径 | 作用 |
|---|---|---|
| POST | `/api/dashboard/login` | Token 或用户名密码登录，建立会话 |
| POST | `/api/dashboard/logout` | 销毁会话 |
| GET | `/api/dashboard/devices` | 设备列表、在线状态、诊断，隐藏 credential 哈希 |
| GET | `/api/dashboard/commands` | 最近 50 条命令；可用 `deviceId` 过滤 |
| POST | `/api/dashboard/devices/:deviceId/power-press` | 创建带幂等键的电源键命令 |
| GET | `/api/dashboard/users` | 用户列表，不返回密码哈希 |
| POST | `/api/dashboard/users` | 创建用户 |
| PATCH | `/api/dashboard/users/:username` | 修改密码或 `enabled` 状态 |
| DELETE | `/api/dashboard/users/:username` | 删除用户，不能删除当前登录用户 |

以下接口只接受管理员 Bearer Token，供初始化和恢复使用：

| 方法 | 路径 | 作用 |
|---|---|---|
| POST | `/api/admin/pairing-codes` | 创建一次性配对码 |
| DELETE | `/api/admin/devices/:deviceId/credential` | 撤销设备 credential，要求重新配对 |
| POST | `/api/admin/devices/:deviceId/power-press` | 管理员 API 创建电源键命令 |
| GET | `/api/admin/commands/:commandId` | 查询单条命令 |

### 7.4 命令状态机

```text
queued -> claimed -> executed
                  -> failed
                  -> rejected
queued -> expired
```

创建命令时生成 UUID 和幂等键。相同设备和幂等键不会重复创建。设备领取时将命令从 `queued` 原子变为 `claimed`；过期命令变为 `expired`。设备不在线时，短命令自然过期，不会重连补发。

## 8. 云端部署拓扑

```text
手机/浏览器
    │ HTTPS
    ▼
Cloudflare DNS + Tunnel
    │ 本机回环 HTTP
    ▼
127.0.0.1:8310
    │
Docker: pc-power-cloud-fastify
    ├─ Fastify API
    ├─ 内嵌管理 HTML
    └─ /data/pc-power.sqlite
```

当前生产信息：

- 公网：`https://pc.19831018.xyz`
- 宿主机：`47.116.199.150`
- 容器：`pc-power-cloud-fastify`
- 容器内部端口：8310
- 宿主机监听：`127.0.0.1:8310`
- 配置：服务器受控文件 `/opt/pc-power-cloud-fastify/service.env`
- 数据目录：`/opt/pc-power-cloud-fastify/data`

Fastify 本身不提供公网 TLS；Cloudflare Tunnel负责公网入口和 HTTPS。设备与云端的连接仍由设备主动建立，服务器不需要给设备开放入站端口。

生产配置至少包括管理员长 Token、数据库路径和监听地址。Token 不写入镜像或代码；数据库目录必须作为持久化卷挂载，更新容器时不能用空目录覆盖旧数据。

## 9. 部署和升级流程

### 9.1 云端

```bash
cd cloud
npm ci
npm test
npm run build
```

部署检查顺序：

1. 保存并核对生产数据卷和 `service.env`。
2. 构建新 Docker 镜像。
3. 用原数据卷启动容器，不执行数据库删除或重建。
4. 检查 `http://127.0.0.1:8310/health`。
5. 检查 `https://pc.19831018.xyz/health` 和首页 HTTP 200。
6. 验证设备继续 `/seen`，再验证管理登录和命令列表。
7. 失败时回滚应用容器镜像，保留数据卷。

### 9.2 固件

ESP-IDF 版本：v5.2.2；芯片：ESP32-C3 revision 0.4；Flash：4 MB。

当前分区：

| 分区 | 地址 | 大小 | 作用 |
|---|---:|---:|---|
| nvs | 0x9000 | 0x6000 | Wi-Fi、认证、云端和 Ping 配置 |
| otadata | 0xF000 | 0x2000 | OTA 启动选择数据 |
| phy_init | 0x11000 | 0x1000 | PHY 初始化 |
| ota_0 | 0x20000 | 0x1D0000 | 应用分区 |
| ota_1 | 0x1F0000 | 0x1D0000 | 备用应用分区 |

当前安全增量烧录只允许写 `0x20000` 应用分区：

```bash
python .../esptool.py --chip esp32c3 --port COM23 --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x20000 firmware/build/pc_power_controller.bin
```

不要执行 `erase_flash`，不要写 bootloader、分区表、NVS、otadata 或 eFuse。当前项目没有网页 OTA 上传接口，不能把串口命令误称为 OTA。后续实现真正 OTA 时必须加入镜像验证、容量检查、写入备用分区、启动确认和失败回滚。

## 10. 数据存储

### 10.1 设备 NVS

| Namespace | Key | 内容 |
|---|---|---|
| Wi-Fi 管理模块使用的配置区 | SSID/密码 | 站点 Wi-Fi 配置，具体 key 以 `wifi_manager.cpp` 为准 |
| `cloud_config` | `url` | 云端 HTTPS 地址 |
| `cloud_config` | `device_id` | 设备 ID |
| `cloud_config` | `credential` | 设备云端 credential |
| `ping_config` | `target` | Ping 目标 |
| 本地认证模块 | 本地账号配置 | 本地 HTTP Basic Auth 所需数据 |

NVS 是设备持久配置，生产增量烧录必须保留。

### 10.2 云端 SQLite

主要表：

- `pairing_codes`：配对码哈希、过期和使用时间。
- `devices`：设备 ID、credential 哈希、配对、撤销、在线时间和诊断 JSON。
- `commands`：命令 ID、设备、幂等键、状态和时间。
- `audit_events`：审计事件。
- `dashboard_users`：用户名、scrypt 密码哈希、启用状态和时间。
- `schema_migrations`：迁移版本。

写数据库使用临时文件导出后原子重命名，数据库权限应限制为服务用户。生产数据库和备份不得提交 Git。

## 11. 启动与运行流程

```text
app_main
  -> nvs_flash_init
  -> local_auth.begin
  -> power_actuator.start (GPIO1=LOW)
  -> environment_sensor.start
  -> power_led_monitor.start
  -> maintenance_console.start
  -> wifi_manager.start
  -> SNTP start
  -> reachability_monitor.start
  -> cloud_client.start
  -> Wi-Fi STA 成功后启动本地 HTTP
  -> 无 STA 时停止本地 HTTP
  -> 配置 AP 活跃时启动 setup HTTP
```

本地 HTTP 和云端客户端是两个独立控制面：本地网页不依赖云端；云端不可达时，本地电源控制仍可使用，只要设备仍在局域网并且 Wi-Fi 正常。

## 12. 测试与验证

云端：

```bash
cd cloud
npm test
npm run build
```

重点覆盖配对一次性使用、credential 哈希和撤销、命令幂等/过期/回执、诊断校验、用户密码哈希、会话认证和管理 API。

固件：

- 先执行编译并确认应用镜像小于 `0x1D0000`。
- 串口烧录只写 `0x20000`，核对芯片 MAC 和写入哈希。
- 启动后观察 GPIO1 无误触发、Wi-Fi、TLS、云端 `/seen`。
- 本地登录后读取 `/api/local/status`。
- 现场确认 GPIO0 亮灭/闪烁与页面一致。
- 在明确确认后只发送一次远程电源命令，核对串口、光耦和电脑实际反应。

## 13. 开发进度与剩余工作

### 已完成

- 项目从旧仓库隔离，建立 ESP-IDF 固件和 TypeScript/Fastify 云端结构。
- 本地认证、Wi-Fi/AP 配网、本地控制和维护控制台完成。
- GPIO1 电源脉冲、GPIO0 PLED 采样、AHT25 温湿度和 Ping 诊断完成。
- 设备主动 HTTPS、一次性配对、credential 哈希、命令领取/回执和 SQLite 持久化完成。
- 云端管理页面、用户密码登录、Token 恢复入口、用户创建/重置/删除完成。
- 诊断快照上报、中文状态映射、移动端响应式页面完成。
- TLS 任务栈提升到 12288 字节并解决握手栈溢出。
- 已构建并烧录 COM23 应用分区，数据哈希校验通过。
- 已推送仓库 `git@github.com:suhongbo2017/ESP32-C3-SUPERMINI-PowerControl.git` 的 `main` 分支。

### 未完成或待现场确认

- 管理页面尚未提供启用/禁用用户的可见按钮，后端 PATCH 已支持。
- 当前没有真正的 OTA 下载/升级端点和发布流水线。
- GPIO0/GPIO1 光耦型号、限流值、输出极性、排针位置和主板接口参数待确认。
- 电脑 PLED 实际亮灭、睡眠闪烁模式和电源键响应待现场确认。
- 需要进一步验证一次真实远程 `power_press` 的物理动作与最终回执。
- 手机主屏 PWA 尚未实现；当前是响应式网页。

## 14. 风险与禁止事项

- 不得把 PLED/PWR_SW 直接接到 ESP32 GPIO。
- 不得在不确认电脑状态时远程点击电源键。
- 不得用 Ping、在线状态或命令 `executed` 推断电脑电源状态。
- 不得执行全片擦除或覆盖 NVS。
- 不得把 Token、密码、配对码、credential、Authorization 头或生产数据库提交到 Git。
- 不得把当前双 OTA 分区描述成已经可用的 OTA 功能。
- 任何现场接线变更都必须同步记录实际光耦型号、阻值、极性和测试结果。
