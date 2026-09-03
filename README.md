# JXQY All-in-One

JXQY All-in-One 是面向《剑侠情缘》系列的跨平台 C++ 运行时与内容编辑工具链。项目的长期目标是以同一套运行时和资源规范支持《剑侠情缘 2》《月影传说》《新剑侠情缘》以及其他兼容 MOD。

当前代码使用 C++17，游戏运行时基于 SDL3，编辑器基于 Qt 6。工程覆盖 Windows、Linux、macOS、iOS 和 Android，并包含资源选择、在线资源目录、资源安装及平台程序更新所需的运行时代码。

## 工程结构

| 路径 | 用途 |
| --- | --- |
| `src/` | 游戏运行时、资源系统、在线更新和跨平台实现 |
| `jxqy-editor/` | Qt/C++ 编辑器与命令行工具 |
| `assets/` | 本地运行资源集合 |
| `android/` | Android Studio/Gradle 工程 |
| `macos_ios/` | macOS 与 iOS Xcode 工程 |
| `linux/` | Linux 依赖准备与构建入口 |
| `win/` | Visual Studio 工程 |
| `starter/` | Windows 便携包图形化启动入口 |
| `updater/` | 桌面程序更新辅助程序 |
| `ThirdParty/` | 源码依赖、依赖清单及本地预编译依赖目录 |
| `licenses/` | 第三方许可证和使用清单 |

## 获取源码

仓库使用 Git submodule 管理 Lua、minilzo 和 miniz：

```sh
git clone --recurse-submodules https://github.com/Upwinded/JXQY-all-in-one.git
cd JXQY-all-in-one
```

已有工作区可执行：

```sh
git submodule update --init --recursive
```

## 预编译依赖

SDL3、SDL3_image、SDL3_ttf、SDL3_mixer 和 FFmpeg 的平台开发库放在 `ThirdParty/devel/`。该目录是本地构建输入，不由主仓库跟踪。

各平台压缩包名称、架构、下载地址和 SHA-256 以 [`ThirdParty/dependencies.ini`](ThirdParty/dependencies.ini) 为准，也可以从 [thirdparty Release](https://github.com/Upwinded/JXQY-all-in-one/releases/tag/thirdparty) 选择对应平台包。

解压后请确认目录结构符合下表：

| 平台 | 依赖目录 | 架构 |
| --- | --- | --- |
| Windows | `ThirdParty/devel/win/` | x86、x64 |
| Android | `ThirdParty/devel/android/` | arm64-v8a、x86_64 |
| Apple | `ThirdParty/devel/mac_ios/` | macOS、iOS 与模拟器 |
| Linux | `ThirdParty/devel/linux/x86_64/` | x86_64 |

Linux x86_64 也可以使用 `linux/build-dependencies.sh` 从锁定版本的上游源码构建依赖。

## 游戏资源

运行时默认从仓库根目录的 `assets/` 读取资源集合。当前 Release 构建至少需要：

```text
assets/
├── engine/
└── resources.ini
```

`resources.ini` 定义集合信息及在线目录地址；各游戏或 MOD 作为资源包放在集合根目录的直接子目录中。完整资源集合不属于源码构建产物，请使用与当前资源规范兼容的资源，并保留原有目录层级和文件名大小写。

Debug 构建可使用完整本地资源集合。Android、macOS 和 iOS 的 Release 构建只嵌入 `engine/` 与 `resources.ini`，其余可玩资源由运行时按资源目录下载到平台可写目录。

## 构建游戏

### Windows

要求 Visual Studio 2022，并准备 `ThirdParty/devel/win/`。

打开：

```text
win/jxqy-all-in-one.sln
```

选择 `x86` 或 `x64` 配置后构建 `jxqy-all-in-one`。对应架构的 SDL3 与 FFmpeg 头文件、导入库和运行库必须完整。

解决方案还包含 `jxqy-starter`，用于生成带项目图标的便携包根启动器。输出位于 `bin/starter/win32/` 或 `bin/starter/win64/`；放到程序包根目录后会启动 `bin/win32/` 或 `bin/win64/` 中的游戏主程序。

### Android

Android 工程当前使用 compileSdk/targetSdk 36，最低系统版本为 Android 9（API 28），构建架构为 `arm64-v8a` 和 `x86_64`。

使用近期 Android Studio 打开 `android/`，准备 Android SDK、NDK、CMake 以及 `ThirdParty/devel/android/`。也可以从命令行构建：

```sh
cd android
./gradlew assembleDebug
```

生成的 APK 会复制到 `bin/android/`。Release 构建使用：

```sh
./gradlew assembleRelease
```

### Linux

当前 Linux 正式目标为 x86_64。Ubuntu/Debian 可先安装平台开发包：

```sh
./linux/install-dependents.sh
```

随后执行：

```sh
./linux/build.sh
```

脚本会检查 `ThirdParty/devel/linux/x86_64/`，缺少时从锁定版本源码构建依赖。游戏输出到 `bin/linux/`，桌面更新辅助程序输出到 `bin/updater/linux/`。

构建完成后可使用根目录启动脚本：

```sh
./'run(put this out of bin).sh'
```

### macOS 与 iOS

准备 `ThirdParty/devel/mac_ios/`，然后用 Xcode 打开：

```text
macos_ios/jxqy/jxqy.xcodeproj
```

共享 Scheme：

- `jxqy macOS`：macOS 10.15+，Release 同时构建 arm64 与 x86_64；
- `jxqy iOS`：iOS 13.0+，支持真机与模拟器。

macOS 工程会通过 Swift Package Manager 解析 Sparkle。Xcode 的构建位置应使用默认设置。Release 构建前请确认 `assets/engine/` 和 `assets/resources.ini` 已准备完整。

命令行验证 macOS 工程时可使用：

```sh
xcodebuild \
  -project macos_ios/jxqy/jxqy.xcodeproj \
  -scheme "jxqy macOS" \
  -configuration Release \
  CODE_SIGNING_ALLOWED=NO \
  build
```

## 构建编辑器

`jxqy-editor` 提供资源迁移与转换、脚本语法检查、资源工程管理，以及地图、菜单、NPC、OBJ、脚本等内容编辑能力。工程同时生成桌面 GUI 和 `jxqy-editor-cli`。

依赖：

- CMake 3.15+；
- 支持 C++17 的编译器；
- Qt 6 Core、Gui、Widgets、Multimedia、LinguistTools；
- 非 Windows 平台需要 Iconv。

构建示例：

```sh
cmake \
  -S jxqy-editor \
  -B build/editor-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt

cmake --build build/editor-release --parallel
```

如果 Qt 已位于 CMake 默认搜索路径，可以省略 `CMAKE_PREFIX_PATH`。

## 开发约定

- 游戏运行时功能优先在 `src/` 实现；资源格式和内容工具优先在 `jxqy-editor/` 实现。
- 资源格式改动应同时验证编辑器输出与运行时读取行为。
- `ThirdParty/devel/` 只保存本地平台依赖。
- `build*`、`jxqy-editor/build*`、DerivedData、APK、DMG、IPA、归档和调试符号均为本地输出。
- 提交前请检查 `git status`，确保源码提交只包含预期文件。

## 主要第三方组件

- [SDL3](https://www.libsdl.org/)、SDL3_image、SDL3_ttf、SDL3_mixer
- [FFmpeg](https://ffmpeg.org/)
- [Lua](https://www.lua.org/)
- [minilzo](https://www.oberhumer.com/opensource/lzo/)
- [miniz](https://github.com/richgel999/miniz)
- [Qt](https://www.qt.io/)（编辑器）
- [Sparkle](https://sparkle-project.org/)（macOS）

具体版本和许可证请查看 [`licenses/THIRD_PARTY_BUILD_DEPENDENCIES.md`](licenses/THIRD_PARTY_BUILD_DEPENDENCIES.md) 与 [`licenses/THIRD_PARTY_NOTICES.md`](licenses/THIRD_PARTY_NOTICES.md)。

## 许可证

项目源码使用 GNU General Public License v3.0，完整条款见 [`LICENSE`](LICENSE)。第三方组件适用各自许可证。

## 感谢

剑侠情缘2这个游戏是我玩的第一个武侠游戏，能够重制这款游戏一直是我埋在心里的梦想。大概3、4年前我曾经尝试使用delphi和lazarus分别做过一些，但由于水平有限同时时间也不允许，引擎只实现了显示地图，之后就搁置了。2017年10月，偶像（weyl，scarsty，bt，sb500）发布了金群的C++复刻版，他在发布时说过：这个引擎有没有人使用不那么重要，重要的是完成了自己一直的梦想。这句话一下子点燃了我重拾梦想、再次开始制作剑侠情缘引擎的想法。我的编程启蒙老师是偶像，当初拿着他的pascal版代码研究学习，一字一句的研读和练习，受益良多，而此次的剑侠情缘重制，我又参考了他的C++复刻版，只能用感激涕零来表达对他的感谢！

剑侠情缘游戏的大部分资料参考了月影传说高清版作者小试刀剑发布的资料以及剑侠情缘贴吧里面的资料，感谢小试刀剑以及各位剑侠情缘贴吧吧友的分享。

这是我第一次用C++写大型的、完整的程序，代码如果存在冗余或者不合理等问题，还请见谅。
