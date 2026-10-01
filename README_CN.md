# LG KDZ 固件工具

**中文** | [English](README.md)

一个高性能、跨平台的命令行工具，用于解包（extract）和重新打包（repack）LG 官方固件（`.kdz`）。本工具用现代 C++ 编写，面向需要检查或修改 LG 设备固件的进阶用户、开发者和研究人员。

## 概览

LG 的官方固件使用一种名为 KDZ 的私有容器格式分发。KDZ 文件内部，核心系统镜像装在 `.dz` 归档里，而 `.dz` 本身又是压缩分区数据块（chunk）的容器。本工具提供完整方案：把这些文件拆解成基础组件，并且可以把它们重新组装回一个可以刷入设备的合法 KDZ 文件。

工具使用多线程加速压缩/解压缩这类计算密集的操作，显著提升处理速度。

## 特性

  - **完整 KDZ 支持：** 解析并生成 KDZ 格式 V1、V2、V3。
  - **固件解包：** 从 KDZ 中提取全部内容，包括：
      - 主体 `.dz` 固件归档；
      - 附带的分发库文件（`.dll` / `.dylib`）；
      - `SecurePartition` 安全分区块；
      - V3 特有的附加映射表（`suffix_map`、`sku_map` 等）。
  - **分区镜像重建：** 把 `.dz` 内的压缩数据块重建成完整的分区镜像（如 `system.img`、`boot.img`），正确处理稀疏（sparse）布局与空洞。
  - **固件重新打包：** 把解包目录（含被修改过的分区镜像）重新打成单个可刷写的 KDZ 文件，并重新计算校验和。
  - **高性能：** 通过线程池并行做压缩（`zlib`/`zstd`）与解压缩，充分吃满 CPU。
  - **元数据管理：** 解包时生成完整的 `metadata.json`，描述原 KDZ 的全部结构；它也是重新打包的唯一依据。
  - **跨平台：** 基于 CMake 构建，可在 Windows、macOS、Linux 上编译运行。

## 工作原理

工具提供两个主命令：`extract` 和 `repack`。

#### 解包流程

1.  **解析 KDZ 头：** 读取 KDZ 主头，识别版本（V1/V2/V3），定位 `.dz` 归档和随附的 `.dll` 等组件。
2.  **解析 DZ 与安全分区：** 解析 `SecurePartition` 块和 `.dz` 主头，校验 magic 与校验和。
3.  **并行解压：** 解压是主要耗时环节，每个压缩数据块交给线程池中的一个工作线程。
4.  **重建镜像：** 数据块解压后写入对应输出镜像（如 `0.boot.img`）中正确的稀疏偏移，从而还原出完整大小的分区镜像。
5.  **提取附属文件：** `.dll`、`.dylib`、`suffix_map.dat` 等被提取到 `components` 子目录。
6.  **生成元数据：** 最后把偏移、大小、校验和、版本信息、分区布局等全部结构信息写入可读的 `metadata.json`。

#### 重新打包流程

1.  **读取元数据：** 重新打包完全由解包目录中的 `metadata.json` 驱动。
2.  **并行压缩：** 读取原始分区镜像（`.img`），按元数据切分成数据块，并行压缩。
3.  **重建 DZ 归档：** 计算压缩数据的 MD5，在内存中组装出新的 `.dz`，并生成更新过 `chunk_hdrs_hash`、`data_hash`、`header_crc` 的新 DZ 主头。
4.  **重建安全分区：** 按元数据重建 `SecurePartition` 块。
5.  **组装最终 KDZ：** 写出重建的 `.dz`、`SecurePartition` 以及 `components` 目录中的组件，位置与原文件一致。
6.  **写入最终头：** 数据就位后偏移和大小都已确定，最后构造 KDZ 头（V1/V2/V3）写到文件开头。

## 获取成品

不想编译的话可以直接用成品：每个打了 tag 的 Release 里都有 GitHub Actions 构建好的二进制
（每次 workflow 运行的 artifact 里也有同样几份）。

| 平台 | 文件 | 说明 |
| :--- | :--- | :--- |
| Windows 10/11 x86_64 | `kdz-tool-windows-x86_64.exe` | 单文件，无需安装其它东西 |
| Linux x86_64 | `kdz-tool-linux-x86_64` | 依赖发行版的 `libz.so.1` 与 `libzstd.so.1` |
| Android arm64-v8a | `kdz-tool-android-arm64-v8a` | Android 7.0（API 24）及以上 |
| Android armeabi-v7a | `kdz-tool-android-armeabi-v7a` | 较老的 32 位机型 |

二进制旁边附有 `SHA256SUMS.txt` 校验文件。

### Windows

```powershell
# 从 Releases 页面下载 kdz-tool-windows-x86_64.exe，然后：
.\kdz-tool-windows-x86_64.exe extract firmware.kdz -d out --rawprogram
.\kdz-tool-windows-x86_64.exe repack out my_firmware.kdz
```

嫌名字长可以改名为 `kdz-tool.exe`。该可执行文件是静态链接的，不需要 VC++ 运行库或 MinGW DLL。

### Linux

```bash
chmod +x kdz-tool-linux-x86_64
./kdz-tool-linux-x86_64 extract firmware.kdz -d out --rawprogram
./kdz-tool-linux-x86_64 repack out my_firmware.kdz
```

该二进制链接发行版的 zlib 与 zstd，精简系统上先装一下：
`sudo apt install zlib1g libzstd1`（Debian/Ubuntu）或 `sudo dnf install zlib libzstd`（Fedora）。

### Android

```bash
adb push kdz-tool-android-arm64-v8a /data/local/tmp/kdz-tool
adb shell chmod +x /data/local/tmp/kdz-tool
adb shell /data/local/tmp/kdz-tool extract /sdcard/Download/firmware.kdz -d /data/local/tmp/out --rawprogram
adb pull /data/local/tmp/out .
```

  - 选对 ABI：`adb shell getprop ro.product.cpu.abi` 会输出 `arm64-v8a` 或 `armeabi-v7a`。
  - 输出目录要可写：`/data/local/tmp` 一定可写，Termux 的 home 也可以；写入 `/sdcard` 需要存储权限（Termux 里执行 `termux-setup-storage`）。
  - 没有 root 的设备无法用本工具刷机，它只负责解包与打包。
  - Android 产物面向 API 24（Android 7.0）及以上。

### macOS

CI 目前不构建 macOS 版本；请按下文的编译步骤自行编译（`brew install cmake zstd` 提供依赖）。

## 依赖

编译本项目需要：

  - 支持 C++17 的编译器（GCC、Clang、MSVC 均可）
  - CMake（3.15 或更高）
  - **Zlib** 开发库（头文件与库）
  - **Zstandard (zstd)** 开发库（头文件与库）

## 编译

Windows / Linux / Android 的成品由 GitHub Actions 自动构建，并附加到每个打了 tag 的 [Release](https://github.com/Fer1d/KDZTool/releases) 上。

用标准 C++ 编译器加 CMake 即可。例如在 Linux 下：

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

Windows 下用 MSYS2 UCRT64 或 MinGW-w64 时，可以这样得到一个不依赖额外 DLL 的静态可执行文件：

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
      -DZLIB_USE_STATIC_LIBS=ON -DCMAKE_EXE_LINKER_FLAGS="-static"
cmake --build build -j
```

### 安卓（Android）端编译

本工具只用 C++17 加 zlib/zstd，在安卓上有两种编法。

**方法一：直接在手机上用 Termux 编译（推荐，无需交叉编译）**

```bash
pkg install clang cmake ninja pkg-config zlib zstd git
git clone https://github.com/Fer1d/KDZTool.git
cd KDZTool
termux-setup-storage                      # 授予 /sdcard 访问权限
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/kdz-tool extract /sdcard/Download/fw.kdz -d /sdcard/Download/out --rawprogram
```

Termux 自带 `zlib.pc` 与 `libzstd.pc`，正好是 CMake 在非 Windows 平台查找的东西；它安装的 clang 也是正常的 C++17 编译器。

**方法二：用 Android NDK 交叉编译（以 arm64-v8a 为例）**

NDK 的 sysroot 自带 zlib，但 zstd 必须先为目标架构编一份：

```bash
export NDK=$HOME/android-ndk-r27
export TOOLCHAIN=$NDK/build/cmake/android.toolchain.cmake
export ABI=arm64-v8a
export PREFIX=$PWD/android-prefix

# 1) 为 Android 编译 zstd（静态库 + pkg-config 文件）
git clone --depth 1 https://github.com/facebook/zstd.git
cmake -S zstd/build/cmake -B zstd-build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 \
      -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release \
      -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_SHARED=OFF
cmake --build zstd-build -j && cmake --install zstd-build

# 2) 让 pkg-config 指向上面那份 zstd，再编本工具（CMakeLists 无需改动）
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/
cmake -S . -B build-android -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-android -j

# 3) 推到设备上运行
adb push build-android/kdz-tool /data/local/tmp/
adb shell chmod +x /data/local/tmp/kdz-tool
adb shell /data/local/tmp/kdz-tool extract /sdcard/Download/fw.kdz -d /data/local/tmp/out --rawprogram
```

说明：

  - 模拟器用 `-DANDROID_ABI=x86_64`，32 位设备用 `armeabi-v7a`。
  - `-DANDROID_PLATFORM=android-24` 是为了 `std::filesystem` 与 `timegm`，更低的 API 等级可能编不过。
  - 工具需要读写固件，所以要放在可写目录里跑：`/data/local/tmp`（adb 推拉）或 Termux 的 home。
  - 安卓没有 root 权限时**不能**直接刷机：本工具只负责解包/打包，刷写仍需 QFIL、LGUP 之类的工具。

## 用法

命令行提供两个主命令：`extract` 与 `repack`。下面的示例统一写作 `kdz-tool`，请替换成你下载到的文件名（见 [获取成品](#获取成品)）。

```
A tool to extract and repack LG KDZ firmware.
Usage: ./kdz-tool <command> [options]

Commands:
  extract    Extract a KDZ file to a folder.
  repack     Repack an extracted folder into a KDZ file.

General Options:
  -h, --help           Show this help message and exit.
```

### 解包 KDZ

解析 KDZ 并把内容提取到指定目录。若不指定目录，只打印头部信息、不写任何文件。

**语法：**

```
./kdz-tool extract <kdz_file> [-d <path>] [--no-verify] [--strict] [--rawprogram] [--keep-b] [--sector-size <bytes>]
```

  - `<kdz_file>`：输入 KDZ 固件路径。
  - `-d, --dest <path>`：提取输出目录。
  - `--no-verify`：（可选）跳过 DZ 数据的完整哈希校验，启动更快，适合只看信息。
  - `--strict`：（可选）把"一致性差异"重新当作致命错误：校验和不匹配、字段取值异常、`part_start_sector` 与分区布局不符等。
  - `--rawprogram`：（可选）在分区镜像旁额外生成 `rawprogram<N>.xml` 与 `patch<N>.xml`，使该目录可直接用于 9008/EDL（QFIL）刷机。必须配合 `-d`。
  - `--keep-b`：（可选）配合 `--rawprogram`：改为正常提取 A/B 设备的 B 槽，而不是用 A 槽的镜像刷入。
  - `--sector-size <bytes>`：（可选）覆盖自动探测出的扇区大小，必须是 512 到 65536 之间的 2 的幂。

#### 一致性警告与真正的错误

默认情况下，解析器只在**结构性损坏**时中止：文件被截断、magic 不对、压缩方式没有对应解压器。凡是"文件里的值与我们**推算**出的值不一致"的情况（头部 CRC32、数据块头 MD5、数据 MD5、编译日期里的星期、本应为 0 的字段、`part_start_sector`），都只记一条警告并继续解析，而且**以文件里存的值作准**。所有发现都会列进 `metadata.json` 的 `diagnostics` 数组；加 `--strict` 即可恢复"一遇到不一致就中止"的老行为。

#### 生成 9008/EDL（QFIL）刷机包

加上 `--rawprogram` 后，解包命令会在分区镜像旁额外写出高通兼容的元数据：

  - `rawprogram<N>.xml`：物理分区 `N`（LUN）的每一段数据一条 `<program>`，带上起始扇区、扇区数、大小和 `SECTOR_SIZE_IN_BYTES`。
  - 只有"按块拆分能省下 32 MiB 以上空洞"的分区才拆成一个块一个小文件（文件名即数据块名）：userdata（10 GiB 分区里只有 6 MiB 数据）和 `system_b` 会拆，而 `oem_a` 这种 4 MiB 的分区仍输出单个 `oem_a.img`；其余都是整分区 `<分区名>.img`。这正是 LG 自己那套包的布局，能让 9008 包的体积等于**数据量**而不是分区大小。
  - 文件名不再带序号前缀：LUN 0 不加前缀（`system_a.img`、`userdata_1.img_3702662`），其余 LUN 用参考工具那套字母前缀（LUN 1 是 `B.`、LUN 2 是 `C.` …）。
  - A/B 设备的 B 槽**完全不提取**：它的条目直接刷 A 槽的镜像（`system_b` 写 `system_a.img` 到 B 槽的位置），LG 自己的包就是这么做的。若 DZ 里 B 槽的数据与 A 槽不同，运行时会明确提示；加 `--keep-b` 则改为正常提取 B 槽。
  - 块元数据（相对偏移、数据大小、每块 MD5）证明内容完全相同的分区也不会重复提取：LUN 2 的 `xbl_b` 直接刷 LUN 1 的 `B.xbl_a.img`。
  - 其余整分区镜像若内容逐字节相同，也共用一个文件。
  - 末端正好落在磁盘末尾的条目（备份 GPT）用 `NUM_DISK_SECTORS` 占位符定位，目标容量更大时会跟着移动；其余条目仍写固定扇区号。
  - `patch<N>.xml`：用于把"填充剩余容量"的那个分区扩到实际磁盘大小。与目标容量相关的值使用 `NUM_DISK_SECTORS` 占位符，由 QFIL 在刷机时代换。若固件里没有可增长的分区，该文件只包含一个空的 `<patches>`。

由于稀疏分区改成了数据块文件，整分区镜像只在**不加** `--rawprogram` 的普通解包时输出 —— `repack` 需要的也正是那种布局。

扇区大小从固件内嵌的 GPT 读取。LG 设备只用 512 字节（eMMC）和 4096 字节（UFS）两种逻辑块，因此探测只尝试这两个值，并按 DZ 头里 `is_ufs` 标志暗示的那个值优先；若 GPT 结论与该标志矛盾，或与"按数据块大小投票"的启发式结论矛盾，都会记录为警告。找不到 GPT 时退回启发式，`--sector-size` 优先级最高——它也是强制其它奇特扇区大小的唯一途径。完整的 QFIL 刷机还需要配套的 firehose 引导程序（`prog_*.mbn`），它不在 KDZ 里。

关于 `sparse` 需要说明一点：DZ 数据块头里的 `is_sparse` **并不表示** payload 是 Android sparse 镜像（它表示该分区在 DZ 里是带空洞存储的）。因此生成器改为检查镜像文件本身，只有真的存在 Android sparse magic 时才写 `sparse="true"`，此时该条目描述的是展开后的分区大小。

**示例：**

```bash
./kdz-tool extract G850UM20A_00_NAO_US_OP_0416.kdz -d G850_extracted
```

解包后目录结构大致如下：

```
G850_extracted/
├── 0.PrimaryGPT.img
├── 0.abl.img
├── 0.boot.img
├── ...（其余分区镜像）
├── rawprogram0.xml        # 仅在加了 --rawprogram 时生成
├── patch0.xml             # 仅在加了 --rawprogram 时生成
├── components/
│   ├── LGE_COMMON.dll
│   ├── LGE_VER.dll
│   ├── suffix_map.dat
│   └── ...（其余组件）
└── metadata.json
```

### 重新打包目录

根据解包目录（含分区镜像、组件和 `metadata.json`）重建 KDZ。

**语法：**

```
./kdz-tool repack <input_dir> <output_file> [--compression <n>]
```

  - `<input_dir>`：包含解包文件与 `metadata.json` 的目录。
  - `<output_file>`：输出的新 KDZ 路径。
  - `--compression <n>`：（可选）压缩级别，1（最快）到 22（最小）；默认用压缩库自己的默认值（zlib 为 6、zstd 为 3）。以 G710 固件实测，`--compression 1` 打包快约 25%，KDZ 体积约大 6%。

打包是流式的：数据块以有限的在途任务数压缩，并直接写进输出文件，所以无论归档多大，重打包一个 8 GB 固件的内存峰值都在 1.5 GB 左右。

**示例：**

```bash
./kdz-tool repack G850_extracted my_custom_firmware.kdz
./kdz-tool repack G850_extracted my_custom_firmware.kdz --compression 1
```

## 许可证

本项目使用 MIT 许可证，详见 [LICENSE](LICENSE)。

## 致谢

本工具依赖以下优秀的开源库：

  - [**nlohmann/json**](https://github.com/nlohmann/json)：简单可靠的 JSON 解析与序列化。
  - [**zlib**](https://www.zlib.net/)：处理 `zlib` 压缩。
  - [**Zstandard (zstd)**](https://facebook.github.io/zstd/)：处理 `zstd` 压缩。
  - MD5 实现参考 **bzflag** 的版本，见 [www.zedwood.com](http://www.zedwood.com/article/cpp-md5-function)。

-----

## 附录：头部结构与字段详解

### KDZ 文件结构（`KdzHeader`）

#### KDZ 头（V1 版本）
总大小：1304 字节

| 字段名 | 起始偏移 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | 头部大小，固定 1304 |
| `magic` | 4 | 4 | Unsigned Int | 魔数，固定 `0x50447932` |
| `dz_record` | 8 | 264 | Struct | DZ 文件记录（见下表） |
| `dll_record` | 272 | 264 | Struct | DLL 文件记录（见下表） |
| `padding` | 536 | 768 | Byte Array | 零填充 |

**V1 记录结构（`V1_RECORD_FMT`）**
大小：264 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | 文件名（以 NUL 结尾的 ASCII 字符串） |
| `size` | 4 | Unsigned Int | 文件大小 |
| `offset` | 4 | Unsigned Int | 文件在 KDZ 中的起始偏移 |

---

#### KDZ 头（V2 版本）
总大小：1320 字节

| 字段名 | 起始偏移 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | 头部大小，固定 1320 |
| `magic` | 4 | 4 | Unsigned Int | 魔数，固定 `0x80253134` |
| `dz_record` | 8 | 272 | Struct | DZ 文件记录（见下表） |
| `dll_record` | 280 | 272 | Struct | DLL 文件记录（见下表） |
| `marker` | 552 | 1 | Byte | 标记字节，通常为 `0x00` 或 `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib 文件记录（见下表） |
| `unknown_record` | 825 | 272 | Struct | 未知记录（通常为空，见下表） |
| `padding` | 1097 | 223 | Byte Array | 零填充 |

**V2/V3 记录结构（`V2_RECORD_FMT`）**
大小：272 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | 文件名（以 NUL 结尾的 ASCII 字符串） |
| `size` | 8 | Unsigned Long | 文件大小（升级为 64 位以支持大于 4GB 的文件） |
| `offset` | 8 | Unsigned Long | 文件在 KDZ 中的起始偏移（升级为 64 位） |

---

#### KDZ 头（V3 版本）
总大小：1320 字节

| 字段名 | 起始偏移 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | 头部大小，固定 1320 |
| `magic` | 4 | 4 | Unsigned Int | 魔数，固定 `0x25223824` |
| `dz_record` | 8 | 272 | Struct | DZ 文件记录（结构与 V2 相同） |
| `dll_record` | 280 | 272 | Struct | DLL 文件记录（结构与 V2 相同） |
| `marker` | 552 | 1 | Byte | 标记字节，通常为 `0x00` 或 `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib 文件记录（结构与 V2 相同） |
| `unknown_record` | 825 | 272 | Struct | 未知记录（通常为空，结构与 V2 相同） |
| `extended_mem_id_size` | 1097 | 4 | Unsigned Int | 扩展内存 ID 的大小 |
| `tag` | 1101 | 5 | Char Array | 标签信息（例如 "5.19"） |
| `additional_records_size` | 1106 | 8 | Unsigned Long | 全部附加记录的总大小 |
| `suffix_map_offset` | 1114 | 8 | Unsigned Long | Suffix Map 偏移 |
| `suffix_map_size` | 1122 | 4 | Unsigned Int | Suffix Map 大小 |
| `sku_map_offset` | 1126 | 8 | Unsigned Long | SKU Map 偏移 |
| `sku_map_size` | 1134 | 4 | Unsigned Int | SKU Map 大小 |
| `ftm_model_name` | 1138 | 32 | Char Array | FTM（工厂测试模式）机型名 |
| `extended_sku_map_offset` | 1170 | 8 | Unsigned Long | 扩展 SKU Map 偏移 |
| `extended_sku_map_size` | 1178 | 4 | Unsigned Int | 扩展 SKU Map 大小 |
| `padding` | 1182 | 138 | Byte Array | 零填充 |

---

### 安全分区结构（`SecurePartition`）
总大小：82448 字节，固定偏移：1320

#### 安全分区头
大小：528 字节

| 字段名 | 起始偏移 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| `magic` | 0 | 4 | Unsigned Int | 魔数，固定 `0x53430799` |
| `flags` | 4 | 4 | Unsigned Int | 标志位 |
| `part_count` | 8 | 4 | Unsigned Int | 分区记录总数 |
| `sig_size` | 12 | 4 | Unsigned Int | 签名长度 |
| `signature` | 16 | 512 | Byte Array | 安全签名（最长 512 字节） |

#### 安全分区记录结构
大小：80 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `name` | 30 | Char Array | 分区名（以 NUL 结尾的 ASCII 字符串） |
| `hw_part` | 1 | Byte | 硬件分区号 |
| `logical_part` | 1 | Byte | 逻辑分区号 |
| `start_sect` | 4 | Unsigned Int | 起始扇区 |
| `end_sect` | 4 | Unsigned Int | 结束扇区 |
| `data_sect_cnt` | 4 | Unsigned Int | 数据占用的扇区数 |
| `reserved` | 4 | Unsigned Int | 保留字段，应为 0 |
| `hash` | 32 | Byte Array | 分区校验值（SHA-256） |

---

### DZ 文件结构（`DzHeader`）

#### DZ 主头（`HDR_FMT`）
总大小：512 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | DZ 头魔数，固定 `0x74189632` |
| `major` | 4 | Unsigned Int | DZ 格式主版本号 |
| `minor` | 4 | Unsigned Int | DZ 格式次版本号 |
| `reserved` | 4 | Unsigned Int | 保留字段，应为 0 |
| `model_name` | 32 | Char Array | 设备机型名 |
| `sw_version` | 128 | Char Array | 软件版本号 |
| `build_date` | 16 | 8x Unsigned Short | 编译日期（年/月/星期/日/时/分/秒/毫秒） |
| `part_count` | 4 | Unsigned Int | 分区数据块总数 |
| `chunk_hdrs_hash` | 16 | Byte Array | 全部数据块头的 MD5 |
| `secure_image_type` | 1 | Byte | 安全镜像类型 |
| `compression` | 9 | Char Array / Byte | 压缩类型（`zlib`/`zstd` 或 1/4） |
| `data_hash` | 16 | Byte Array | 全部数据的 MD5 |
| `swfv` | 50 | Char Array | 软件固件版本（SWFV） |
| `build_type` | 16 | Char Array | 编译类型（USER/DEBUG） |
| `unknown_0` | 4 | Unsigned Int | 未知字段，应为 0 |
| `header_crc` | 4 | Unsigned Int | DZ 主头的 CRC32 |
| `android_ver` | 10 | Char Array | Android 版本号 |
| `memory_size` | 11 | Char Array | 内存大小 |
| `signed_security` | 4 | Char Array | 是否为安全签名（`Y` 或 `N`） |
| `is_ufs` | 4 | Unsigned Int | 是否为 UFS 存储（非 0 表示是） |
| `anti_rollback_ver` | 4 | Unsigned Int | 防回滚版本号 |
| `supported_mem` | 64 | Char Array | 支持的存储类型列表 |
| `target_product` | 24 | Char Array | 目标产品名 |
| `multi_panel_mask` | 1 | Byte | 多面板支持位掩码 |
| `product_fuse_id` | 1 | Byte | 产品熔丝 ID（0-9 的 ASCII 数字） |
| `unknown_1` | 4 | Unsigned Int | 未知字段，应为 0 或 `0xFFFFFFFF` |
| `is_factory_image` | 1 | Byte | 是否为工厂固件（ASCII `F` 表示是） |
| `operator_code` | 24 | Char Array | 运营商代码 |
| `unknown_2` | 4 | Unsigned Int | 未知字段，应为 0 或 1 |
| `padding` | 44 | Byte Array | 零填充 |

#### DZ 数据块头（V0 版本）
总大小：124 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | 数据块魔数，固定 `0x78951230` |
| `part_name` | 32 | Char Array | 所属分区名 |
| `chunk_name` | 64 | Char Array | 数据块名 |
| `decompressed_size` | 4 | Unsigned Int | 解压后大小 |
| `compressed_size` | 4 | Unsigned Int | 压缩后大小 |
| `hash` | 16 | Byte Array | 数据块数据的 MD5 |

#### DZ 数据块头（V1 版本）
总大小：512 字节

| 字段名 | 大小 | 数据类型 | 说明 |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | 数据块魔数，固定 `0x78951230` |
| `part_name` | 32 | Char Array | 所属分区名（切片名） |
| `chunk_name` | 64 | Char Array | 数据块名 |
| `decompressed_size` | 4 | Unsigned Int | 解压后大小 |
| `compressed_size` | 4 | Unsigned Int | 压缩后大小 |
| `hash` | 16 | Byte Array | 数据块数据的 MD5 |
| `start_sector` | 4 | Unsigned Int | 在设备上的起始扇区 |
| `sector_count` | 4 | Unsigned Int | 占用的扇区数 |
| `hw_partition` | 4 | Unsigned Int | 硬件分区号（LUN） |
| `crc` | 4 | Unsigned Int | 数据块的 CRC32 |
| `unique_part_id` | 4 | Unsigned Int | 唯一分区 ID |
| `is_sparse` | 4 | Unsigned Int | 是否为稀疏镜像 |
| `is_ubi_image` | 4 | Unsigned Int | 是否为 UBI 镜像 |
| `part_start_sector` | 4 | Unsigned Int | 整个分区的起始扇区（重建镜像时的基准） |
| `padding` | 356 | Byte Array | 零填充 |

### 说明
* 所有多字节整数（short、int、long 等）均为 **小端序（Little-endian）**。
* 所有大小单位均为 **字节（Byte）**。