# ESP32-CAM Live

适用于经典 AI-Thinker ESP32-CAM（ESP32-S + OV2640 + ESP32-CAM-MB）的 ESP-IDF 工程。

## 功能

- Wi-Fi SoftAP：`ESP32-CAM` / `12345678`
- 主页：`http://192.168.4.1/`
- MJPEG 视频流：`http://192.168.4.1:81/stream`
- RTSP/RTP 视频流：`rtsp://192.168.4.1:554/mjpeg/1`
- 单张照片：`http://192.168.4.1/snapshot`
- 页面实时显示 FPS 和 MJPEG 码率
- RTSP 固定目标 30 FPS，RTP 使用 90 kHz 标准时间戳
- SoftAP 使用现场扫描并经 RTP 压力测试后吞吐更好的频道 11
- FreeRTOS 独立摄像头采集任务持续取帧
- 长度为 1 的最新帧队列；网络来不及发送时直接丢弃旧帧
- 标准 RFC 2435 JPEG over RTP/UDP，兼容 VLC 和 FFmpeg
- RTP 数据包最大约 1350 字节，避免 IP 层再次分片
- RTP 分片按每 4 包一组提交；仅大运动帧需要分组让出 1 ms，静态小帧不增加等待
- FreeRTOS 使用 1 kHz 系统节拍；摄像头采集与较高优先级的 RTSP 发送运行在 CPU1，Wi-Fi 驱动运行在 CPU0
- RTP 分片发送失败时放弃当前帧，下一帧继续
- 每个 RTP 分片遇到本地发送队列繁忙时最多尝试 3 次，间隔约 0.1 ms
- 画质三档：流畅 / 均衡 / 清晰
- 分辨率三档：320×240 / 400×296 / 640×480
- 默认关闭自动曝光并使用适合 30 FPS 的短曝光；中 / 长档可能降低实际帧率
- 切换画质或分辨率时自动重连视频流，避免旧帧积压
- 原生 Android 客户端，可通过系统弹窗连接 ESP32-CAM SoftAP
- Android 客户端支持实时画面、曝光、分辨率、画质和视频帧拍照
- Android 客户端网络接收和 JPEG 解码解耦，积压时丢弃旧帧并自动恢复中断的视频流

## 工程结构

```text
esp32-cam-live/
├── CMakeLists.txt
├── LICENSE
├── sdkconfig.defaults
├── README.md
├── main/
│   ├── app_main.c          仅负责程序入口和初始化顺序
│   ├── CMakeLists.txt
│   └── idf_component.yml   官方 esp32-camera 依赖声明
├── components/
│   └── esp32_cam_live/
│       ├── include/        对其他组件公开的接口
│       ├── private_include/组件内部配置、引脚和协议头文件
│       ├── src/            摄像头、采集、网络、RTSP 和 Web 实现
│       └── CMakeLists.txt
├── android/               原生 Android 客户端（Android 10 及以上）
├── tools/                 Android 模拟器联调桥接工具
└── third_party/
    └── README.md           第三方依赖来源和管理策略
```

业务实现集中在独立的 `esp32_cam_live` 组件中，入口层不包含协议或硬件细节。
第三方组件由 ESP-IDF Component Manager 下载，不将生成的
`managed_components/` 目录提交到版本库。

## 编译和烧录

在 ESP-IDF 终端中进入本目录：

```powershell
idf.py set-target esp32
idf.py build
idf.py -p COM8 flash monitor
```

退出串口监视器：`Ctrl+]`。

如果端口发生变化，把 `COM8` 换成设备管理器显示的端口。

## 使用

1. 给 ESP32-CAM 上电。
2. 电脑连接 Wi-Fi `ESP32-CAM`，密码 `12345678`。
3. 在 VLC 中选择“媒体 → 打开网络串流”。
4. 输入 `rtsp://192.168.4.1:554/mjpeg/1` 并播放。
5. 浏览器打开 `http://192.168.4.1/` 调整曝光、画质和分辨率。

高分辨率、高清画质和长曝光都会降低实际 FPS；30 FPS 是发送上限，不是硬件保证值。

## Android 客户端

Android 10 及以上设备可以使用仓库中的原生客户端。应用通过 Android 系统的
Wi-Fi 确认窗口连接 `ESP32-CAM`，不会修改手机的全局默认网络，也不会保存密码。

```powershell
cd android
.\gradlew.bat assembleDeviceDebug
```

生成的安装包位于：

```text
android/app/build/outputs/apk/device/debug/app-device-debug.apk
```

应用播放 `http://192.168.4.1:81/stream`，控制请求发送到
`http://192.168.4.1/control`。拍照直接保存当前视频帧到系统相册的
`Pictures/ESP32-CAM`，因此不会额外占用 ESP32 摄像头帧缓冲。

## 开源协议

本项目采用 [MIT License](LICENSE)。
