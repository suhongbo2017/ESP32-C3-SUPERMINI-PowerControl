# ESP32-C3 PC Power Controller

用 ESP32-C3 SuperMini 短按机箱电源键的控制器：网页/按钮触发一次约 300ms 的脉冲，
带 OTA、家庭 Wi-Fi / Home Assistant 接入、Wi-Fi 故障备用 AP，以及可选的 PWR-LED / 温湿度 / MQTT / Ping 集成。

## 固件选型

主固件为 **ESPHome**（`pc-power-c3.yaml`），理由是第二阶段规划的功能
（MQTT、Ping、PWR-LED、温湿度）ESPHome 全部原生支持，且 OTA 带安全模式与回滚。

`src/` 下的 PlatformIO + Arduino 实现是**第一阶段的回退版本**（仅脉冲 + 网页 + OTA + AP），
保留用于救援和对比，不再是主线。两者引脚与硬件接线完全一致，可互相 OTA 覆盖。

## 硬件安全（务必先读）

光耦模块板载的 `30R0` 只有 30Ω，**不足以保护 GPIO**：
3.3V 直连时 LED 电流约 70mA，超出 ESP32-C3 单脚推荐 20mA 和 PC817 的 50mA 上限。

正确接线：

```text
GPIO1 --[220~330Ω]-- 光耦 IN+
GPIO1 --[10kΩ]------ GND            <- 必须：防止上电/复位期间悬空误触发
GND   --------------- 光耦 IN-
光耦 S1/S2 ---------- 主板 PWR_SW 两针
```

阻值计算：`R_总 = (3.3V - Vf) / I_LED`，Vf≈1.2V。

| 情形 | 外部电阻 | 实际 LED 电流 |
|---|---|---|
| 板上 30Ω 在输入回路（先用万用表确认） | 220Ω | ≈7.4mA |
| 板上 30Ω 在输入回路，偏保守 | 330Ω | ≈6.4mA |
| 板上 30Ω 不在输入回路 | 220Ω | ≈8mA |

用万用表确认拓扑：

1. 二极管档，红笔 `IN+`、黑笔 `IN-` → 应显示约 1.0~1.2V（LED 压降）。
2. 电阻档量 `IN+ ↔ IN-` → 若某方向约 30~40Ω，则 30Ω 在输入回路；若两方向都开路，则不在。

上机顺序：先不接主板，触发一次脉冲，串表测 GPIO1 回路电流（<10mA）与高电平电压（>3.0V），
合格后再把 `S1/S2` 接到 `PWR_SW`。

> 更保险的做法：GPIO1 → 1kΩ → S8050 基极，光耦 LED 串在集电极回路由 3.3V 供电，
> GPIO 只出约 1mA，彻底不依赖 GPIO 的驱动能力。

## 烧录（ESPHome，推荐）

```bash
pip install esphome                  # 或使用 Home Assistant 的 ESPHome 插件
cp secrets.yaml.example secrets.yaml # 填入密码 / api_key 等密钥
esphome config pc-power-c3.yaml      # 语法与密码引用检查
esphome run pc-power-c3.yaml         # 首次 USB 烧录；之后可走 OTA
esphome logs pc-power-c3.yaml
```

首次 USB 烧录会编译并自动下载。设备连入家庭 Wi-Fi 后，可通过 ESPHome OTA 更新，
或继续使用 USB 端口烧录。`secrets.yaml` 中必须填写 2.4GHz Wi-Fi 凭据、备用 AP 密码、
Web 登录密码和 API 加密密钥。

Home Assistant 使用说明：

1. 确认 ESP32-C3 与 Home Assistant 在可互相访问的局域网中。
2. 固件成功连接 Wi-Fi 后，在 Home Assistant 中添加 ESPHome 设备 `pc-power-c3`。
3. 按提示输入 `secrets.yaml` 中 `api_key` 对应的 API 加密密钥。
4. 添加后使用实体“PC Power C3 电源键短按”触发约 300ms 的 GPIO1 脉冲。
5. 若家庭 Wi-Fi 不可用，设备启动带密码的备用热点 `PC-Power-C3`，可连接后访问 `http://192.168.4.1/`；网页用户名为 `admin`，密码为 `web_password`。

## 回退到 Arduino 固件（救援用）

```bash
pio run
pio run -t upload                    # 需要 USB 连接
pio device monitor -b 115200
```

旧固件行为：GPIO1 默认 `LOW`，串口命令 `PULSE` / `STATUS` / `HELP`，
网页 `http://<IP>/`，OTA 页面 `http://<IP>/ota`（Basic Auth，用户名 `admin`，
密码见 `src/config.h` 的 `OTA_PASSWORD` —— **该密码已在公开仓库中，请先修改**）。

> 建议在切到 ESPHome 之前先备份整片 flash，以便随时恢复：
> `esphome` 无法直接读取备份，用 esptool：
> `esptool.py --chip esp32c3 read_flash 0 0x400000 backup-full.bin`

## 与旧固件的功能对照

| 功能 | 旧固件 (Arduino) | ESPHome |
|---|---|---|
| GPIO1 300ms 脉冲 | ✅ 非阻塞状态机 | ✅ `switch: gpio` + `delay` |
| 独立网页 | ✅ 自绘暗色 UI | ✅ `web_server` v2（页面资源内置） |
| OTA | ⚠️ 无回滚/安全模式 | ✅ `ota` + `safe_mode` |
| 家庭 Wi-Fi / 备用 AP | ✅ AP 模式 | ✅ `wifi` 接入家庭网络，失败时启动 `wifi.ap` |
| 串口交互命令 | ✅ PULSE/STATUS/HELP | ❌ 仅日志（无 REPL） |
| MQTT / Ping / PWR-LED / 温湿度 | ❌ 未实现 | ✅ 配置文件末尾已备好，取消注释即可 |

## 参考：规划功能（第二阶段）

- **开机状态检测（PWR-LED）**：`binary_sensor: gpio` + `filters: delayed_off` 去抖
- **Ping**：新版 ESPHome 的 `ping` 二进制传感器；老版本用 HA 的 Ping 集成
- **温湿度**：I2C 走 GPIO4(SDA)/GPIO5(SCL)，避开 GPIO8（板载 LED）和 GPIO9（BOOT/strapping）
- **MQTT**：`mqtt:` 组件

以上均已写在 `pc-power-c3.yaml` 末尾（注释状态）。