# Arduino OpenAPS (Milestone 1)

该目录用于存放 Arduino 侧的 OpenAPS 控制器示例代码（Nano 33 IoT）。提供两种实现：
- M1_FreeRTOS.ino：使用 FreeRTOS_SAMD21（`FreeRTOS.h`、`task.h`、`semphr.h`）的多任务版本
- M1_NoRTOS.ino：不依赖 RTOS 的简化轮询版本

前置环境
- Boards Manager：安装 `Arduino SAMD Boards (32-bits ARM Cortex-M0+)`
- Libraries（Library Manager 安装）：
  - `WiFiNINA`
  - `ArduinoMqttClient`
  - `FreeRTOS_SAMD21`（可选；用于多任务版本）

主题与凭据约定
- 前缀：`cis441-541/{TEAM}`（与 Codio 侧一致）
- 话题映射（二选一）：
  - 标准模式：OpenAPS 订阅 `{prefix}/cgm`；发布 `{prefix}/insulin-pump`
  - 桥接模式（需运行 `relay.py`）：订阅 `{prefix}/cgm-openaps`；发布 `{prefix}/insulin-pump-openaps`
- 可选属性握手：`{prefix}/vp-attributes/request/OpenAPS`、`{prefix}/vp-attributes/response/OpenAPS`

使用步骤
1) 在 Arduino IDE 打开本目录下的草稿（任选其一）
2) 替换草稿顶部的 `WIFI_SSID/WIFI_PASS/MQTT_HOST/MQTT_PORT/MQTT_USER/MQTT_PASSWD/TEAM` 为你的实际值
3) 选择开发板：`Arduino Nano 33 IoT`；选择串口
4) 上传并打开串口监视器（115200），确认连接日志
5) 在 Codio 侧启动虚拟患者：`python "virtual patient"/main.py 1`
   - 如使用桥接模式，额外运行：`python "virtual patient"/relay.py`

运行期望
- 收到 CGM：串口打印 `BG/time`
- 按占位规则发布 `insulin_rate` 到 `{prefix}/insulin-pump`（桥接模式下为 `{prefix}/insulin-pump-openaps`）
- Codio 侧桥接日志（如启用）显示 VP→OAPS 与 OAPS→VP 的消息流