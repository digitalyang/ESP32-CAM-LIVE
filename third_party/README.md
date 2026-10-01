# Third-party dependencies

This project does not vendor external source code into the repository.

The official Espressif `esp32-camera` component is declared in
`main/idf_component.yml` and is downloaded by the ESP-IDF Component Manager at
configure time. Exact resolved versions are recorded locally in
`dependencies.lock`, which is intentionally excluded from version control.

Keeping third-party code outside the application component makes ownership,
updates, and license auditing explicit.

The Android application resolves `org.videolan.android:libvlc-all:3.6.5` from
Maven Central. LibVLC is maintained by VideoLAN and distributed under
LGPL-2.1-or-later. Its binaries are not committed to this repository; Gradle
downloads the AAR while building the Android application.
