# ESP8266 网关式 STM32 OTA：问题与解决方案汇总

本文整理本次对话中遇到的硬件连接、烧录、PlatformIO、HTTPS、GitHub Pages、OTA验证、Boot模式与FreeRTOS判断等问题。排障时建议按本文最后的“端到端检查顺序”逐项处理。

> 版本说明：对话中进行OTA验证时，STM32板内实际运行的是 `1.0.0 / 10000`，服务器发布的是 `1.1.0 / 10100`。电脑中后来重新生成的 `factory_bundle.hex` 不能代表板内已经烧录的版本。本文生成时，当前源码 `Common/ota_config.h` 中的版本号已改为 `10301`，后续发布必须以实际构建版本为准。

## 1. 系统方案与关键启动关系

- STM32 Flash 中常驻自定义Bootloader：`0x08000000–0x08007FFF`。
- STM32应用位于：`0x08008000–0x0803EFFF`。
- 元数据位于：`0x0803F000–0x0803FFFF`。
- ESP-01S负责联网、HTTPS下载、LittleFS缓存和USART2传输。
- STM32应用负责判断设备是否处于安全升级状态；自定义Bootloader负责擦除、写入、SHA-256与ECDSA校验。
- 首次必须通过ST-Link烧录包含Bootloader、应用和元数据的 `factory_bundle.hex`；后续STM32固件才可远程升级。
- ESP-01S本方案不支持自身OTA，重新烧录ESP仍需USB-TTL串口。

## 2. ST-Link擦除/下载失败

### 现象

```text
Erase Failed!
ST-LINK USB communication error
Error: Flash Download failed - "Cortex-M3"
```

### 常见原因

1. SWDIO、SWCLK、NRST、GND或目标板3.3V参考电压连接不可靠。
2. 目标板供电不稳定，或者ST-Link与目标板没有共地。
3. Keil中的SWD频率过高。
4. 用户程序快速关闭电源、重映射调试脚或进入异常状态。
5. ST-Link固件、驱动、USB线或USB接口异常。
6. 芯片读保护/选项字节状态异常。

### 处理顺序

1. 只保留最小SWD连接：`3.3V参考、GND、SWDIO、SWCLK、NRST`。
2. 给目标板独立稳定供电，并确保与ST-Link共地。
3. 在Keil中选择 `Connect under Reset`，将SWD速度降到 `100 kHz–1 MHz`。
4. 按住NRST，再执行连接或下载，连接建立后松开NRST。
5. 升级ST-Link固件，更换USB数据线和USB口。
6. 能连接后先读取Option Bytes；确认允许整片擦除时，再执行Full Chip Erase。
7. STM32的 `BOOT0` 保持低电平；不要为了运行自定义Bootloader而切到系统Bootloader。

### 验证标准

- Keil能够稳定识别Cortex-M3。
- 下载日志出现Erase、Program、Verify成功。
- 重新上电后先执行 `0x08000000` 处的自定义Bootloader。

## 3. 如何让电机旋转并验证业务功能

### 软件操作

当前按键逻辑为：

```text
短按MODE：OFF → MANUAL → AUTO → OFF
手动模式短按SPEED：STOP → LOW → MID → HIGH → STOP
```

因此最简单的电机测试步骤是：

1. 系统上电并保持自锁供电。
2. 短按一次MODE进入 `MANUAL`。
3. 短按SPEED进入 `LOW`，再次短按进入 `MID/HIGH`。
4. 观察串口中的 `KEY MODE`、`KEY SPEED`、`MOTOR target`、`PWM`日志。

### 硬件条件

- `PA0/TIM2_CH1`连接TB6612的PWMA。
- `PB12/PB13`控制电机方向。
- `PB14`连接TB6612的STBY；运行时必须为高电平。
- TB6612逻辑电源、STM32和电机电源必须共地。
- 电机不能由STM32 GPIO直接驱动，必须经过电机驱动器。
- 检查TB6612的VM电机电源和VCC逻辑电源是否正确。

### 验证标准

- 手动模式下SPEED档位切换有对应日志。
- PA0能够测到PWM波形。
- PB14运行时为高电平。
- 驱动器输出端存在与PWM档位相符的电压变化。

## 4. ESP-01S固件如何烧录

### 结论

ESP-01S首次或重新烧录固件必须通过USB-TTL连接ESP自身的UART0完成，不是由STM32给ESP下载固件。

### 烧录模式

```text
GPIO0接GND
EN/CH_PD保持高电平
RST保持高电平
重新上电或复位
```

然后在VS Code终端或PowerShell中运行：

```powershell
cd ESP8266_Gateway
pio run -t upload --upload-port COM10
```

烧录成功后：

```text
GPIO0断开GND
重新复位ESP
进入正常Flash运行模式
```

烧录期间应断开或隔离STM32的PA2/PA3，避免STM32和USB-TTL同时驱动ESP的GPIO1/GPIO3。

### 成功标志

```text
Writing at ...
Hash of data verified.
SUCCESS
```

仅出现编译成功不代表已经烧入ESP。

## 5. `pio`命令无法识别

### 现象

```text
pio : 无法将“pio”项识别为 cmdlet、函数、脚本文件或可运行程序
```

### 原因

- PlatformIO Core尚未安装。
- PlatformIO已经由VS Code扩展安装，但其虚拟环境目录没有加入PATH。

### 解决方案

优先使用VS Code中的 `PlatformIO: New Terminal`。也可直接运行扩展安装的CLI：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" --version
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

若本机Python环境正常，也可以安装PlatformIO Core：

```powershell
py -m pip install -U platformio
py -m platformio --version
```

## 6. PlatformIO Home Server超时

### 现象

```text
Could not start PIO Home server: Timeout error
```

### 解决方案

1. 完全退出VS Code并结束残留PlatformIO进程，然后重新打开工程。
2. 更新或重新安装PlatformIO IDE扩展。
3. 使用PlatformIO终端直接运行CLI，不依赖PIO Home网页：

   ```powershell
   pio run
   pio run -t upload --upload-port COM10
   ```

4. 检查杀毒软件、防火墙是否拦截本机回环端口。
5. 检查 `%USERPROFILE%\.platformio` 是否有写权限以及磁盘空间是否充足。

PIO Home打不开不一定影响命令行编译和上传。

## 7. ESP上传超时

### 现象

```text
Connecting........_____.....
Failed to connect to ESP8266: Timed out waiting for packet header
```

### 主要原因

ESP没有进入UART下载模式。

### 解决步骤

1. 烧录前将GPIO0可靠接地。
2. 断电重上电，或将RST短暂拉低后释放。
3. 断开STM32与ESP的TX/RX，避免总线冲突。
4. 确认USB-TTL使用3.3V逻辑，TX/RX交叉连接并共地。
5. ESP由独立稳定3.3V电源供电，供电能力至少500mA。
6. 确认COM端口没有被串口助手占用。
7. 烧录完成后GPIO0必须恢复高电平/悬空并再次复位。

## 8. 串口乱码

### 现象

```text
c|▒▒▒r▒#▒c▒▒'o...
```

### 原因

1. ESP8266 ROM启动日志常使用 `74880` 波特率。
2. 正常OTA串口使用 `115200, 8N1`，传输内容是二进制帧，不是可读文本。
3. 波特率不匹配时正常文本也会显示成乱码。

### 处理方法

- 查看ESP上电ROM日志时尝试 `74880`。
- 查看STM32调试日志时使用USART1：`PA9 → USB-TTL RX，115200, 8N1`。
- STM32与ESP之间的USART2是二进制OTA通道，不应使用普通串口助手按文本理解。
- 当前ESP代码另外提供GPIO2/Serial1单向调试输出，可用USB-TTL RX接GPIO2，波特率115200；注意不要让USB-TTL反向驱动GPIO2。

## 9. ESP-01S原理图中EN/RST/GPIO0/GPIO2悬空

### 结论

若模块确实是ESP-01S，模块通常已经配置启动所需的上拉，EN、RST、GPIO0和GPIO2悬空一般可以正常运行；若是旧ESP-01，则EN悬空可能导致不启动或反复复位。

### 推荐电路

```text
EN     ── 10kΩ ── 3.3V
RST    ── 10kΩ ── 3.3V，并预留到GND的复位按钮
GPIO0  ── 10kΩ ── 3.3V，并预留到GND的烧录跳线
GPIO2  ── 保持启动时为高电平
VCC附近放置10µF + 0.1µF去耦
```

ESP与STM32连接：

```text
ESP GPIO1/TX → STM32 PA3/USART2_RX
ESP GPIO3/RX ← STM32 PA2/USART2_TX
GND          ↔ GND
```

## 10. HTTPS清单、CA证书和GitHub Pages

### `OTA_MANIFEST_URL`

实际配置为：

```cpp
#define OTA_MANIFEST_URL \
    "https://kttui.github.io/hood-ota/stable/manifest.json"
```

### CA证书填写方式

必须粘贴完整PEM证书，包括头尾：

```cpp
static const char OTA_ROOT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
完整Base64证书正文
-----END CERTIFICATE-----
)EOF";
```

禁止使用 `setInsecure()`。BearSSL校验证书前必须先通过SNTP获得正确时间。

### 哈希值不是证书

类似下面的64位十六进制字符串：

```text
32491b26bfaf7bfb556029ef32299ac4374db34d09eecd4ffd01f21b2c5a8567
```

是SHA-256摘要或证书指纹，不是可直接放入 `OTA_ROOT_CA` 的证书。证书必须包含 `BEGIN CERTIFICATE` 和 `END CERTIFICATE`。

### 根CA层级

证书链顶部的 `ISRG Root X1` 是根CA；其下是中间CA，最下层是站点证书。对话中使用的GitHub Pages证书链由Let’s Encrypt签发。

### `.nojekyll`

`.nojekyll` 是空文件，用来告诉GitHub Pages不要使用Jekyll处理站点。它不会生成首页。根地址返回404不一定影响OTA，关键是以下具体文件必须返回200：

```text
https://kttui.github.io/hood-ota/stable/manifest.json
https://kttui.github.io/hood-ota/stable/stm32-1.1.0.bin
```

对话中已实际确认上述清单和1.1.0固件均返回HTTP 200，固件长度为34756字节。

## 11. 长按UPGRADE后没有进入更新

### 首先明确按键行为

长按UPGRADE超过1.5秒只表示“立即检查远程固件”，不是无条件强制进入Bootloader。

正常日志：

```text
[INFO] KEY UPGRADE long press -> check remote firmware
[INFO] OTA manual check requested
```

这两条日志只证明STM32已经识别按键并发出检查请求，不能证明ESP已经联网、下载并返回 `OTA_PREPARE`。

### 安装必须满足的条件

```text
mode == SYS_MODE_OFF
current_rpm == 0
target_rpm == 0
motor.enabled == 0
power_off_request == 0
```

这里的OFF是“系统仍有电但处于逻辑OFF”，不是整机彻底断电。彻底断电后ESP也无法联网。

### 版本判断

- 板端 `10000`、服务器 `10100`：允许升级。
- 板端和服务器都是 `10100`：正常应用有效时不升级。
- 服务器版本更低：拒绝降级。

不能用电脑上后来生成的 `factory_bundle.hex` 推断板内版本。需要以实际烧录文件、UI版本显示、STM32 `APP_BOOT_OK`或ESP日志为准。

### 启动握手丢失问题

早期实现中，STM32只在启动时发送一次 `APP_BOOT_OK`。ESP启动比STM32慢时可能错过该帧，导致：

```text
appOnline == false
→ ESP可以检查/缓存固件
→ 但不发送OTA_PREPARE
→ STM32不进入Bootloader
```

临时验证方法：

1. 整机上电并等待ESP约20秒。
2. 只复位STM32，不要断电或复位ESP。
3. 系统保持逻辑OFF且风机停止。
4. 再长按UPGRADE。

当前源码已经改进：每次手动检查前会重新发送 `APP_BOOT_OK`，并输出等待进度和超时原因，从而避免这一上电时序问题。

### 当前STM32侧预期日志

```text
OTA USART2 ready: TX=PA2 RX=PA3 baud=115200
OTA TX APP_BOOT_OK: version=...
OTA TX APP_BOOT_OK resync: version=...
OTA TX CHECK: waiting ESP response (timeout=90s)
OTA waiting: 10s rx_bytes=... valid_frames=...
OTA RX PREPARE: server_version=... size=... local=...
OTA accepted: version=... size=...
```

超时日志解释：

- `no ESP response`：ESP未运行、未联网、接线错误或没有更高版本。
- `RX bytes exist but no valid frame`：波特率、TX/RX冲突或数据损坏。
- `valid frame received, no PREPARE`：ESP有通信，但清单检查/下载/版本判断未产生准备通知。

## 12. OTA时是否需要切换BOOT模式

### STM32

不需要切换，且 `BOOT0` 必须一直为低电平：

```text
BOOT0 = 0
→ 从内部Flash 0x08000000启动
→ 执行自定义OTA Bootloader
```

`BOOT0=1` 会进入STM32厂家ROM系统Bootloader，不会进入本项目的自定义Bootloader。

### ESP8266

- 正常运行OTA网关：GPIO0高电平或悬空。
- USB-TTL重新烧录ESP：GPIO0在复位时接地。

## 13. STM32的BOOT0是否默认为0

BOOT0不能依赖悬空状态。推荐：

```text
BOOT0 ── 10kΩ ── GND
```

如果开发板已有BOOT0跳线，将跳线放在GND端即可。对于只使用ST-Link首次烧录、后续使用自定义OTA Bootloader的产品，也可以永久保持BOOT0低电平。

上电测量标准：

- 接近0V：正确。
- 接近3.3V：将进入STM32系统Bootloader。
- 电压漂移：BOOT0悬空，应增加下拉。

## 14. “程序好像没有进入FreeRTOS”

### 已确认结论

程序已经进入FreeRTOS。以下两条日志本身就是证据：

```text
[INFO] KEY UPGRADE long press -> check remote firmware
[INFO] OTA manual check requested
```

它们依赖完整的FreeRTOS任务链：

```text
key_task
→ FreeRTOS消息队列
→ control_task
→ FreeRTOS任务通知
→ ota_task
→ 日志队列
→ log_task打印
```

若调度器未启动，上述跨任务日志不可能产生。

### Keil调试误区

`vTaskStartScheduler()`正常情况下不会返回，因此在它后面设置的断点不会命中。程序停在以下位置也可能只是正常的任务切换：

```text
SVC_Handler
PendSV_Handler
SysTick_Handler
xPortPendSVHandler
xPortSysTickHandler
```

### 验证方法

1. 在 `start_task()`、`key_task()`和 `ota_task()`中设置断点。
2. 观察FreeRTOS变量 `xTickCount` 是否持续增加。
3. 观察周期传感器、UI、电机控制或OTA等待日志。

### 构建产物不一致

排查时还发现某次：

```text
Objects/Project.bin        14:22生成
Release/factory_bundle.hex 11:13生成
```

这说明工厂包没有包含后来重新编译的应用。修改应用后必须重新执行发布脚本，不能只编译Keil工程后继续烧录旧的 `factory_bundle.hex`。

## 15. 正确的OTA发布与验证流程

### 发布

1. 修改 `Common/ota_config.h` 中的 `OTA_APP_VERSION_CODE`，必须单调递增。
2. 在Keil中重新编译应用，确认生成最新的 `Objects/Project.bin`。
3. 运行：

   ```powershell
   python tools/release_firmware.py release `
     --version 1.2.0 `
     --version-code 10200 `
     --base-url https://kttui.github.io/hood-ota/stable
   ```

4. 验证发布包：

   ```powershell
   python tools/verify_release.py
   ```

5. 先上传版本化 `.bin`，确认可访问后最后替换 `manifest.json`。
6. 做OTA验证时不要把新生成的 `factory_bundle.hex` 再烧进STM32，否则板端会提前变成与服务器相同版本。

### 正常升级过程

```text
STM32应用发送APP_BOOT_OK
→ ESP读取当前STM32版本
→ ESP联网和SNTP校时
→ HTTPS获取manifest
→ 判断服务器版本更高
→ 下载到/fw.tmp
→ 校验长度和SHA-256
→ 改名为/stm32.bin
→ ESP发送OTA_PREPARE
→ STM32在安全状态下发送OTA_ACCEPT并软件复位
→ 自定义Bootloader发送BL_HELLO
→ ESP发送BEGIN/DATA/END
→ Bootloader校验哈希和ECDSA签名
→ 元数据切换为VALID
→ 跳转新应用
→ 新应用发送APP_BOOT_OK
→ ESP删除缓存
```

## 16. 推荐的端到端排障顺序

1. **确认电源和BOOT状态**
   - STM32 BOOT0低电平。
   - ESP GPIO0高电平/悬空、EN高、RST高。
   - ESP使用稳定3.3V/500mA以上电源。

2. **确认STM32应用与FreeRTOS**
   - USART1/PA9能够看到启动和任务日志。
   - 长按UPGRADE出现按键与OTA任务日志。

3. **确认USART2物理链路**
   - PA2→ESP RX，PA3←ESP TX，共地。
   - USB-TTL不与STM32同时驱动ESP UART0。

4. **确认ESP运行状态**
   - GPIO2/Serial1调试口出现 `[BOOT] ESP8266 OTA gateway started`。
   - 出现Wi-Fi连接、NTP同步日志。

5. **确认服务器**
   - manifest和版本化bin均返回HTTP 200。
   - target、version_code、size、sha256、signature、url字段正确。

6. **确认版本关系**
   - 服务器 `version_code` 必须严格高于当前有效应用。

7. **确认安全安装条件**
   - 系统逻辑OFF、转速0、目标转速0、电机关闭、无关机请求。

8. **确认Bootloader传输**
   - 观察BL_HELLO、BEGIN ACK、DATA进度、END RESULT。

9. **确认新应用启动**
   - 新版本UI/日志发生预期变化。
   - ESP收到新版本 `APP_BOOT_OK` 并删除缓存。

## 17. 当前最重要的结论

1. 长按UPGRADE日志已经证明FreeRTOS任务调度正常。
2. STM32 OTA期间不需要切换BOOT0；BOOT0应固定为低电平。
3. ESP正常运行时GPIO0不能接地；只有USB-TTL烧录时才拉低。
4. `pio run -t upload` 在VS Code终端或PowerShell运行，不是在串口助手中运行。
5. GitHub Pages根页面404不等于OTA文件不可用；应检查具体manifest和bin URL。
6. SHA-256摘要/指纹不是PEM CA证书。
7. 长按更新只是立即检查，不是强制升级；版本、安全状态、网络、HTTPS、串口和握手必须全部通过。
8. 修改应用后必须重新生成发布包，避免 `Project.bin` 与 `factory_bundle.hex` 不是同一次构建。

