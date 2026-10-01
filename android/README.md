# ESP32-CAM Live for Android

原生 Java Android 客户端，最低支持 Android 10（API 29）。

## 功能

- 通过 Android `WifiNetworkSpecifier` 请求连接 `ESP32-CAM`
- 兼容 Android 16 / HyperOS 的网络变更权限检查，系统拒绝请求时不会闪退
- 内嵌官方 VideoLAN LibVLC，播放标准 RTSP + RTP/JPEG over UDP 视频
- 显示 ESP32 实际完成发送的 FPS 和 JPEG 码率
- RTSP 启动失败后自动重试，播放器原生缓存用于平滑无线抖动
- 自动曝光开关与 100 / 300 / 600 三档手动曝光
- QVGA / CIF / VGA 三档分辨率
- 流畅 / 均衡 / 清晰三档 JPEG 画质
- 使用 Android PixelCopy 将当前播放器画面保存到 `Pictures/ESP32-CAM`

## 构建

安装 Android SDK 34 和 JDK 17 后执行：

```powershell
.\gradlew.bat assembleDeviceDebug
```

真机调试 APK 输出到
`app/build/outputs/apk/device/debug/app-device-debug.apk`。

## 模拟器测试

Android Emulator 无法直接加入电脑附近的实体 SoftAP。仓库提供独立的
`emulatorDebug` 构建目标和开发桥接工具，避免模拟器地址进入真机版本：

```powershell
python ..\tools\emulator_bridge.py
.\gradlew.bat assembleEmulatorDebug
```

模拟器版本通过宿主机地址 `10.0.2.2` 访问桥接端口；正式和真机调试版本始终
直接访问 `192.168.4.1`。

模拟器只能验证视频、控制和拍照链路；Android 系统的 SoftAP 连接确认流程仍需
在实体手机上验证。

## 使用

1. 打开 ESP32-CAM 并等待 SoftAP 启动。
2. 启动应用，点击“连接 ESP32-CAM”。
3. 在 Android 系统弹窗中确认连接。
4. 视频出现后即可调整参数或点击“拍照”。

手机可能提示此 Wi-Fi 无互联网，这是正常现象。应用进程在播放期间绑定到
ESP32-CAM 的 Wi-Fi `Network`，保证 LibVLC 的原生 RTSP/RTP 套接字走相机网络；
断开后立即解除绑定。

## 第三方播放器

应用通过 Maven Central 使用 `org.videolan.android:libvlc-all:3.6.5`。LibVLC
由 VideoLAN 提供并采用 LGPL-2.1-or-later；项目自身代码仍采用 MIT License。
