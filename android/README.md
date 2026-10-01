# ESP32-CAM Live for Android

原生 Java Android 客户端，最低支持 Android 10（API 29）。

## 功能

- 通过 Android `WifiNetworkSpecifier` 请求连接 `ESP32-CAM`
- 播放 ESP32-CAM 的 HTTP MJPEG 实时视频
- 显示客户端实际解码 FPS 和 JPEG 码率
- 自动曝光开关与 100 / 300 / 600 三档手动曝光
- QVGA / CIF / VGA 三档分辨率
- 流畅 / 均衡 / 清晰三档 JPEG 画质
- 将当前视频帧保存到 `Pictures/ESP32-CAM`

## 构建

安装 Android SDK 34 和 JDK 17 后执行：

```powershell
.\gradlew.bat assembleDebug
```

调试 APK 输出到 `app/build/outputs/apk/debug/app-debug.apk`。

## 使用

1. 打开 ESP32-CAM 并等待 SoftAP 启动。
2. 启动应用，点击“连接 ESP32-CAM”。
3. 在 Android 系统弹窗中确认连接。
4. 视频出现后即可调整参数或点击“拍照”。

手机可能提示此 Wi-Fi 无互联网，这是正常现象。应用将 HTTP 请求明确绑定到
ESP32-CAM 的 Wi-Fi `Network`，移动数据仍可由系统用于其他应用。
