# MiRob CAM

`MiRob CAM` 是一个基于 ESP32 平台的嵌入式相机项目，用于实现本地拍摄、存储与远程图像上报能力。项目面向机器人视觉采集场景，强调轻量部署、板载交互和可扩展的模块化结构。

当前仓库包含两个子工程：

- `MiRob_cam_s3`：ESP32-S3 主版本，支持 TFT 实时显示、TF 卡照片/视频保存、WiFi 联网后通过 MQTT 向服务器推送 JPEG 预览帧（Mode4），以及按键与状态灯交互。
- `MiRob_cam_espcam`：ESP32-CAM 兼容版本，提供精简的拍照与网页预览能力，便于在资源受限硬件上快速落地。

## 项目定位

- 面向低功耗边缘端的图像采集与上报
- 兼顾「本地可见」（屏幕/按键）与「网络上报」（MQTT 图像主题）
- 通过统一的模块划分（`camera` / `storage` / `network` / `mqtt` / `display` / `log` 等）降低移植与维护成本

## 核心能力概览

- 相机采集：支持多种拍摄模式与参数配置
- 本地存储：将拍摄结果保存到 TF 卡
- 实时预览：板载 TFT 显示；Mode4 下按配置帧间隔向 MQTT 主题发布 JPEG（需在 `MiRob_cam_s3/include/config.h` 中配置 WiFi、Broker、主题等）
- 设备交互：支持按键控制、串口调试与状态指示

## MiRob_cam_s3：Mode4（MQTT）配置提示

在 `include/config.h` 中填写或修改：

- `MQTT_WIFI_SSID` / `MQTT_WIFI_PASSWORD`：设备作为 STA 接入的路由器
- `MQTT_BROKER_HOST` / `MQTT_BROKER_PORT`：MQTT 服务器地址与端口
- `MQTT_CLIENT_ID`、`MQTT_USER`、`MQTT_PASSWORD`（可选）、`MQTT_IMAGE_TOPIC`：连接与图像发布主题

图像为原始 JPEG 二进制负载，订阅端按 JPEG 解码即可。帧率与体积受 `CAM_PREVIEW_*` 与 `CAM_PREVIEW_FRAME_DELAY_MS` 等参数影响。

## 目录结构

```text
MiRob_cam/
├─ MiRob_cam_s3/       # ESP32-S3 主工程
├─ MiRob_cam_espcam/   # ESP32-CAM 兼容工程
├─ model/              # 外壳模型文件
├─ pcb/                # pcb工程文件
└─ README.md
```
