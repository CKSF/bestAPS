# Project 1 OpenAPS – Milestone 2

本仓库包含三个核心组件：
- Virtual Patient (VP)：仿真患者模型与 MQTT 交互（`virtual patient/`）
- Virtual Component (VC)：话题桥接器（`virtual component/`）
- Arduino OpenAPS 控制器（必须使用 FreeRTOS 版本，`adurino/M1_FreeRTOS/M1_FreeRTOS.ino`）

本 README 说明代码结构、运行方式、话题映射、以及 Milestone 2 的评估与日志要求。请搭配 `M2_Notes.txt` 使用，以便完整理解任务与验收标准。

## 目录结构
- `virtual patient/`
  - `main.py`：VP 主程序，订阅胰岛素、发布 CGM，并支持属性握手与患者档案加载
  - `patient_profile.json`：默认患者档案；可替换为不同场景进行评估
  - `.env_template`：环境变量模板（复制为 `.env` 并填写）
  - `mqtt.py`、`bergman_model.py`、`insulin_model.py`、`meals_model.py`：模型与 MQTT 封装
  - `requirement.txt`：Python 依赖
- `virtual component/`
  - `main.cpp`：VC 桥接程序，连接 MQTT 并桥接两个方向的话题
  - `makefile`：编译脚本
- `adurino/`
  - `M1_FreeRTOS/M1_FreeRTOS.ino`：Arduino OpenAPS（FreeRTOS 版本，Milestone 2 必做）
  - `M1_NoRTOS/M1_NoRTOS.ino`：M1 简化版参考（保留以参考，不用于 M2 验收）

## 话题映射与凭据
- 前缀：`cis441-541/{TEAM_NAME}`（从环境变量读取）
- VP 侧（按 `.env_template`）
  - 订阅胰岛素：`{prefix}/insulin-pump`
  - 发布 CGM：`{prefix}/cgm`
  - 属性握手：`{prefix}/vp-attributes/{request|response}/openaps`
- VC 桥接（main.cpp）
  - OpenAPS → VP：`{prefix}/insulin-pump-openaps` → `{prefix}/insulin-pump`
  - VP → OpenAPS：`{prefix}/cgm` → `{prefix}/cgm-openaps`
- Arduino（FreeRTOS 版本）
  - 发布胰岛素：`{prefix}/insulin-pump-openaps`
  - 订阅 CGM：`{prefix}/cgm-openaps`
  - 属性握手请求/响应：同上

## 组件作用与设计要点
- VC（`virtual component/main.cpp`）
  - 负责桥接两组话题：OpenAPS→VP 与 VP→OpenAPS
  - 已移除订阅的 `->wait()` 阻塞，以避免在 Paho C++ 异步模型下卡死；订阅在连接成功后进行，重连后自动恢复（如需可扩展回调）
- VP（`virtual patient/main.py`）
  - 读取 `.env` 加载 MQTT 参数与话题，加载 `patient_profile.json`
  - 模拟主循环发布 CGM（`Glucose` 与 `time` 字段），并处理胰岛素消息（收到时打印 `+`）
  - 支持属性握手：接收 OpenAPS 请求、响应患者档案（含 `bolus_insulins`）
- Arduino（`M1_FreeRTOS/M1_FreeRTOS.ino`）
  - Milestone 2 必须实现 OpenAPS 核心逻辑（详见 `M2_Notes.txt`）：
    - `insulin_calculations(t)`：基于历史治疗计算 total activity 与 IOB
    - `get_BG_forcast(current_BG, activity, IOB)`：计算 `naive_eventual_BG` 与 `eventual_BG`
    - `get_basal_rate(t, current_BG)`：用上述结果决定基础胰岛素速率，并追加到治疗集合
  - MQTT 回调：
    - 属性话题：解析 `bolus_insulins`，创建 `InsulinTreatment` 并调用 `addInsulinTreatment`；置标志位并退订属性话题
    - CGM 数据：解析 `Glucose` 与 `time`，更新 `current_BG/current_time`，置 `newBGData`
  - FreeRTOS 任务：
    - `TaskOpenAPS` 读取新数据、计算并发布 `insulin_rate` 到 `{prefix}/insulin-pump-openaps`；使用互斥保护数据

## 环境与依赖
- Python（VP）
  - 安装依赖：`pip install -r "virtual patient"/requirement.txt`
  - 配置 `.env`：复制 `.env_template`，填写 `TEAM_NAME/MQTT_HOST/MQTT_PORT/USERNAME/PASSWORD`
- C++（VC）
  - 需安装：`libpaho-mqtt3a-dev`、`libpaho-mqttpp3-dev`
  - 编译：`cd "virtual component" && make`
- Arduino（OpenAPS）
  - Boards：`Arduino SAMD Boards (32-bits ARM Cortex-M0+)`
  - Libraries：`WiFiNINA`、`ArduinoMqttClient`、`FreeRTOS_SAMD21`

## 运行步骤（验收固定顺序）
1) 启动 VP
```bash
cd "virtual patient"
python3 main.py 1
```
2) 启动 VC
```bash
cd "virtual component"
make && ./main
```
3) 上传 Arduino（FreeRTOS 版本）并打开串口监视器（115200）
- 修改草稿顶部凭据：`WIFI_SSID/WIFI_PASS/MQTT_HOST/MQTT_PORT/MQTT_USER/MQTT_PASSWD/TEAM`
- 观察日志：属性握手响应、CGM 收到、发布 `insulin_rate`

## 日志打印与评估要求（核心）
- 默认患者 profile 下，展示四类“Basal Decision Logic”分支，打印变量随时间：
  - `currentBG, eventualBG, naive_eventual_BG, basal_rate, IOB, activity, t`
  - 分支：
    - Case #1: `currentBG < threshold` 或 `eventualBG < threshold`
    - Case #2-1: `threshold <= eventualBG < target` 且 `naive_eventual_BG < 40`
    - Case #2-2: `threshold <= eventualBG < target` 且 `naive_eventual_BG >= 40`
    - Case #3: `eventualBG >= target`
- 其他场景评估：
  - 非糖尿病档案：分别“有/无 OpenAPS”对比
  - 错误场景：
    - 额外 bolus 无餐（删除某次餐）
    - 缺失餐的 bolus（删除某次 bolus）
- 产出：每个场景的档案文件与“仿真结束 dashboard 截图”（或曲线图），以及运行日志片段

## 常见问题
- 订阅阻塞：VC 已移除 `->wait()`，避免异步订阅卡死
- 话题不匹配：确保 Arduino 发布到 `insulin-pump-openaps`，VP 订阅 `insulin-pump`（经 VC 桥接），Arduino 订阅 `cgm-openaps`
- 档案同步：VP 启动会提示等待 OpenAPS 同步；属性握手请求应由 Arduino 发出并处理 `bolus_insulins`

---
如需更详细的任务拆解与验收对照，请查看根目录的 `M2_Notes.txt`。