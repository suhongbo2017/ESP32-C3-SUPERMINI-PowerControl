# NVS 加密与设备密钥生命周期

## 决策状态

设计记录，尚未启用生产配置，也未在任何设备上烧录 eFuse。

ESP32-C3 支持 HMAC peripheral。ESP-IDF v5.2.2 可使用 HMAC eFuse key 派生 NVS XTS 密钥，因此保护本地账号摘要、Wi-Fi 凭据和 Ping 目标不要求同时启用整片 flash encryption。首选在发布固件中启用默认 NVS 加密并采用 HMAC 保护方案；是否启用 Secure Boot 和 flash encryption 是独立的量产安全决策。

## 当前工程边界

- 当前开发默认配置不启用 NVS encryption，不配置 HMAC eFuse key slot。
- 当前 4 MB 分区表的 NVS 区为普通 `nvs`；HMAC 方案不需要独立的 `nvs_keys` 分区。
- 应用通过 `nvs_flash_init()` 初始化默认 NVS，`LocalAuth` 和 `WifiManager` 都使用该默认分区。
- 加密启用后，默认 NVS 初始化会在所选 eFuse slot 未配置时生成 HMAC key，并生成 NVS 加密密钥。此流程会写入不可逆 eFuse，必须由量产 provisioning 流程明确控制。
- 禁止把通用 HMAC eFuse key、设备凭据或任何私钥写入仓库、日志或固件默认配置。

## 开发设备

继续使用未加密的开发构建和可擦写开发板。验证 `nvs_flash_init()` 错误处理、凭据读写与恢复逻辑时，不烧录 HMAC eFuse key，不将开发设备用于生产安全验收。

需要测试加密集成时，使用专用、可报废的 ESP32-C3 开发板和独立实验固件；先检查并保存 `espefuse.py summary` 输出，确认该板不会用于其他用途。测试 eFuse 是不可逆操作，不能把这类测试当作普通开发步骤。

## 量产配置实施前提

1. 决定 HMAC eFuse key slot 的所有权，并确认没有与 Secure Boot、其他 HMAC 用途或后续功能冲突。
2. 创建独立 release sdkconfig/defaults，启用 `CONFIG_NVS_ENCRYPTION`、选择 HMAC 保护方案并明确设置 `CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID`；不得改动默认开发配置。
3. 由设备 provisioning 程序或受控的首次启动流程写入每台设备独有的 HMAC key。量产批次必须有设备身份、eFuse 写入结果和测试结果的审计记录。
4. 校验 NVS 初始化错误时不得继续启动本地控制、Wi-Fi 凭据写入或云端凭据处理；不得回退到未加密 NVS。
5. 验证首次初始化、正常重启、OTA 后读取、掉电中断、NVS 损坏和整片擦除后的恢复行为。
6. 确认 USB Serial JTAG 维护链路在启用的安全配置下仍满足现场维护需求。Secure Boot/flash encryption、下载模式与安全启动策略须单独验证。
7. 只有独立 release 构建、provisioning 流程和板级验收通过后，才能将本 Ticket 的 NVS 安全验收标记完成。

## 恢复和返修

HMAC eFuse key 不可恢复或轮换。整片擦除会删除 NVS 数据，但不会清除 eFuse key；设备再次启动时可以在同一 key 下重新初始化空 NVS。更换设备、丢失或损坏芯片不能从备份恢复原设备密钥，必须重新配对并设置本地凭据。禁止把原设备 NVS 镜像复制到另一台设备作为恢复手段。

在启用 Secure Boot 或 flash encryption 后，返修、串口下载、OTA 回滚与固件降级必须按其独立策略执行。本记录不提供会烧录 eFuse 的命令，所有不可逆 provisioning 命令须在后续发布流程 ticket 中评审并加入人工确认步骤。

## 参考依据

- ESP-IDF v5.2.2 `docs/zh_CN/api-reference/storage/nvs_encryption.rst`
- ESP-IDF v5.2.2 `docs/en/migration-guides/release-5.x/5.2/storage.rst`
- ESP-IDF v5.2.2 `components/nvs_flash/Kconfig`
- ESP32-C3 SoC capability: `SOC_HMAC_SUPPORTED=1`
