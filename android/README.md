# Android 构建

## 构建类型

| Android Studio Build Variant | Gradle 任务 | 内置资源 |
| --- | --- | --- |
| `debug` | `:app:assembleDebug` | 本地全部资源，开启调试 |
| `release` | `:app:assembleRelease` | `engine` 和 `resources.ini` |
| `fullyRelease` | `:app:assembleFullyRelease` | 本地全部资源，使用 Release 配置 |

`fullyRelease` 是 `fully-release` 在 Gradle 和 Android Studio 中的名称，继承
`release` 的签名与编译配置，关闭调试，包含 `arm64-v8a`、`x86_64` 两种架构。
三种构建使用相同应用包名和版本；全资源包使用正式证书签名后可覆盖同证书的普通 Release。

全资源来自仓库根目录 `assets/`，包括 `engine`、`common`、三部曲和本地 MOD；本地存在的
测试 MOD 也随目录打包。沿用 Debug 的资源排除规则，不打包个人存档、用户配置、日志、
归档压缩包、Git 元数据和迁移临时文件；`ini/save` 等新游戏模板保留。

在 Android Studio 同步工程后，从 Build Variants 选择 `fullyRelease`。命令行在
`android/` 执行：

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat :app:assembleFullyRelease --no-daemon
```

APK 自动复制到 `bin/android/jxqy-all-in-one_fully-release_v<version>.apk`。
签名沿用 `release` 的四个 `jxqyRelease*` Gradle 属性或 `JXQY_ANDROID_*` 环境变量；
未配置时生成未签名 APK。资源直接内置 APK，
安装后可离线选择内置游戏。包体和安装所需空间随本地资源总量增长。

内置资源发现由项目的 Android 资源适配代码完成：取得当前 APK 路径后，使用已有
miniz 读取 ZIP 中央目录，从文件名的 `assets/` 前缀后提取第一层目录名并去重，
再通过共享 `ResourceCatalog` 读取各目录的 `game_profile.ini`。
中央目录读入内存后，名称遍历不读取资源内容或各文件的本地头；枚举完成即释放
ZIP 元数据。下载/导入资源继续使用应用专属目录，同 `Game.Id` 的可写资源保持
原有覆盖优先级。Android 依赖使用 `ThirdParty/devel` 中的官方 SDL 3.4.10 AAR。

启动时先准备内置引擎字体并呈现“正在加载资源…”等待页，再扫描本地资源。
扫描结束后显示资源选择页，并丢弃等待期间积压的鼠标和触摸事件。

打包后的资源数量校验不能替代运行时发现校验。在没有已下载/外部资源的测试设备上
安装 APK、断开网络后，可从仓库根目录运行下列命令。脚本重启游戏并核对原生启动日志，
不清空设备数据；应发现 APK 中全部内置资源，且资源页可直接进入本地游戏。

```powershell
python scripts/check_android_bundled_resources.py --serial emulator-5580 --apk <APK路径> --log tmp/android-bundled-discovery/startup.log
```

检查普通 `release` 的零内置游戏场景时加 `--allow-empty`；默认仍要求 APK 含游戏资源。
