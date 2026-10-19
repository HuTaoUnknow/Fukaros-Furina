# Third-party components

- FFmpeg 7.1.1 public headers: https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz . License notices are preserved in third_party/ffmpeg. Windows dynamically loads the FFmpeg 7.1.3 DLLs supplied with the installed Qt 6.11.1 kit; the API headers are from 7.1.1; applications do not modify these DLLs. Qt / FFmpeg component details: https://doc.qt.io/qt-6/qtmultimedia-attribution-ffmpeg.html .
- NVIDIA NVENC API 12.1 header: https://github.com/FFmpeg/nv-codec-headers/tree/n12.1.14.0 . The header's MIT notice is preserved; the NVIDIA driver binary is loaded from Windows System32 and is not bundled.
- Qt and MinGW runtime deployment uses the existing Qt installation and windeployqt.

- VirtualDrivers Virtual-Display-Driver 25.7.23 official AMD64 driver package: https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/tag/25.7.23 . MIT license retained at Fukaros/core/display/virtual-display/driver/LICENSE.txt. Signed files are distributed unmodified and installation validates fixed SHA-256 hashes and Authenticode.

- Qt 6.11.1 is dynamically linked. LGPL v3 / GPL v3 license texts accompany the Windows runtime; upstream source: https://download.qt.io/archive/qt/6.11/6.11.1/single/ . Qt module and bundled third-party notices: https://doc.qt.io/qt-6/licenses-used-in-qt.html . Applications do not modify Qt libraries.
- Windows installer includes the SDK's unmodified Android platform-tools ADB and its NOTICE.txt. Source: https://android.googlesource.com/platform/packages/modules/adb/ .
- MinGW runtime: dynamically deployed from the Qt MinGW 13.1 kit; GCC runtime exception applies to libgcc / libstdc++: https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html .
- FFmpeg binaries preserve their license notices; corresponding FFmpeg 7.1.3 source: https://ffmpeg.org/releases/ffmpeg-7.1.3.tar.xz . Bundled FFmpeg source reference and build configuration are recorded in the package's third-party files.

## Replacement and rebuilding

Qt and FFmpeg are shared libraries. Users may replace them with interface-compatible modified libraries, and may rebuild the application and reverse engineer it to debug those library changes as permitted by the LGPL. Build instructions are in the repository README. No signing secret is required to rebuild the APK with your own key. Application code does not modify the Qt or FFmpeg source.

Qt Toolkit: Copyright (C) The Qt Company Ltd. and other contributors. Qt uses LGPL v3; see docs/licenses/COPYING.LGPLv3 and COPYING.GPLv3. The source package is Qt 6.11.1: https://download.qt.io/archive/qt/6.11/6.11.1/single/qt-everywhere-src-6.11.1.tar.xz . Module third-party notices are included with upstream source and listed at https://doc.qt.io/qt-6/licenses-used-in-qt.html .

FFmpeg Windows DLLs: FFmpeg 7.1.3, LGPL 2.1 or later; see third_party/ffmpeg/COPYING.LGPLv2.1 and LICENSE.md. Public headers are from 7.1.1 and use the compatible 7.1 ABI. ADB is copied from Android SDK platform-tools with its upstream NOTICE.txt; source at https://android.googlesource.com/platform/packages/modules/adb/ .

GCC runtime library exception version 3.1: see docs/licenses/GCC-Runtime-Exception-3.1.txt and COPYING.GPLv3. MinGW components are dynamically linked. VirtualDrivers' MIT license is kept in Fukaros/core/display/virtual-display/driver/LICENSE.txt; NVIDIA's header MIT notice is kept in docs/licenses/NVENC-header-license.txt.
