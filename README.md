# LG KDZ Firmware Tool

**English** | [中文说明](README_CN.md)

A high-performance, cross-platform command-line utility for extracting and repacking LG official firmware files (`.kdz`). This tool is written in modern C++ and is designed for power users, developers, and researchers who need to inspect or modify LG device firmware.

## Overview

LG's official firmware is distributed in a proprietary container format known as KDZ. Inside a KDZ file, the core operating system images are contained within a `.dz` archive, which itself is a container for compressed partition chunks. This tool provides a complete solution to deconstruct these files into their base components and, crucially, to rebuild them back into a valid KDZ file that can be flashed to a device.

The tool uses multi-threading to accelerate the computationally intensive tasks of compression and decompression, significantly speeding up the workflow.

## Features

  - **Full KDZ Support:** Parses and builds KDZ format versions V1, V2, and V3.
  - **Firmware Extraction:** Extracts all contents from a KDZ file, including:
      - The main `.dz` firmware archive.
      - Supporting DLLs and dylib files.
      - The `SecurePartition` block.
      - V3-specific metadata maps (`suffix_map`, `sku_map`, etc.).
  - **Partition Image Reconstruction:** Reconstructs full partition images (e.g., `system.img`, `boot.img`) from compressed chunks within the `.dz` file, correctly handling sparse layouts.
  - **Firmware Repacking:** Repacks an extracted directory—including any modified partition images—back into a single, flashable KDZ file with updated checksums.
  - **High Performance:** Leverages a thread pool to perform parallel compression (`zlib`/`zstd`) and decompression, maximizing CPU usage for faster operation.
  - **Metadata Management:** On extraction, generates a comprehensive `metadata.json` file that describes the entire structure of the original KDZ. This file is the blueprint for the repacking process.
  - **Cross-Platform:** Built with CMake, allowing it to be compiled and run on Windows, macOS, and Linux.

## How It Works

The tool operates in two main modes: `extract` and `repack`.

#### Extraction Process

1.  **Parse KDZ Header:** The tool first reads the main KDZ header to identify its version (V1/V2/V3) and locate all primary components like the `.dz` archive and any accompanying `.dll` files.
2.  **Parse DZ & Secure Partition:** It then parses the `SecurePartition` block and the main `.dz` header, verifying magic numbers and checksums to ensure file integrity.
3.  **Decompress in Parallel:** The core task of decompression is parallelized. Each compressed data chunk from the `.dz` file is assigned to a worker thread.
4.  **Reconstruct Images:** As chunks are decompressed, they are written to the correct sparse offset within their corresponding output image file (e.g., `0.boot.img`). This reconstructs the original, full-sized partition images for all the partitions (e.g., `boot`, `system`, `modem`).
5.  **Extract Components:** Ancillary files (`.dll`, `.dylib`, `suffix_map.dat`, etc.) are extracted into a `components` subdirectory.
6.  **Generate Metadata:** Finally, all structural information—offsets, sizes, checksums, version info, partition layouts, and more—is saved to a human-readable `metadata.json` file.

#### Repacking Process

1.  **Read Metadata:** The repacking process is driven entirely by the `metadata.json` file from an extracted firmware directory.
2.  **Compress in Parallel:** The tool reads the raw partition images (`.img`), slices them into chunks according to the metadata, and compresses each chunk in a worker thread.
3.  **Rebuild DZ Archive:** It calculates new MD5 hashes for the compressed chunks and assembles them into a new `.dz` file in memory. A new main DZ header is generated with updated `chunk_hdrs_hash`, `data_hash`, and `header_crc`.
4.  **Rebuild Secure Partition:** The `SecurePartition` block is rebuilt from the information stored in the metadata.
5.  **Assemble Final KDZ:** The tool creates the final KDZ file. It writes the rebuilt `.dz` archive, the `SecurePartition` block, and the other components from the `components` directory at their original offsets.
6.  **Write Final Header:** With all data in place, the final offsets and sizes are known. The tool constructs the definitive KDZ header (V1, V2, or V3) and writes it to the beginning of the file, completing the process.

## Getting the binaries

You do not have to build anything to use the tool: every tagged release carries binaries built by
GitHub Actions (the same files are attached as artifacts of each workflow run).

| Platform | File | Notes |
| :--- | :--- | :--- |
| Windows 10/11 x86_64 | `kdz-tool-windows-x86_64.exe` | one file, nothing else to install |
| Linux x86_64 | `kdz-tool-linux-x86_64` | needs `libz.so.1` and `libzstd.so.1` from the distribution |
| Android arm64-v8a | `kdz-tool-android-arm64-v8a` | Android 7.0 (API 24) or newer |
| Android armeabi-v7a | `kdz-tool-android-armeabi-v7a` | older 32-bit devices |

`SHA256SUMS.txt` is attached next to the binaries.

### Windows

```powershell
# download kdz-tool-windows-x86_64.exe from the Releases page, then
.\kdz-tool-windows-x86_64.exe extract firmware.kdz -d out --rawprogram
.\kdz-tool-windows-x86_64.exe repack out my_firmware.kdz
```

Rename it to `kdz-tool.exe` if you prefer shorter commands. The executable is linked statically,
so no Visual C++ redistributable or MinGW runtime is required.

### Linux

```bash
chmod +x kdz-tool-linux-x86_64
./kdz-tool-linux-x86_64 extract firmware.kdz -d out --rawprogram
./kdz-tool-linux-x86_64 repack out my_firmware.kdz
```

The binary is linked against the distribution's zlib and zstd, so on a minimal system install them
first: `sudo apt install zlib1g libzstd1` (Debian/Ubuntu) or `sudo dnf install zlib libzstd` (Fedora).

### Android

```bash
adb push kdz-tool-android-arm64-v8a /data/local/tmp/kdz-tool
adb shell chmod +x /data/local/tmp/kdz-tool
adb shell /data/local/tmp/kdz-tool extract /sdcard/Download/firmware.kdz -d /data/local/tmp/out --rawprogram
adb pull /data/local/tmp/out .
```

  - Pick the ABI of the device: `adb shell getprop ro.product.cpu.abi` prints `arm64-v8a` or `armeabi-v7a`.
  - Write the output somewhere writable: `/data/local/tmp` always is, a Termux home directory is too, and `/sdcard` needs the storage permission (`termux-setup-storage` in Termux).
  - A device without root cannot flash anything with this tool; it only unpacks and repacks.
  - The Android builds target API 24 (Android 7.0) and newer.

### macOS

The CI does not build for macOS yet; compile it from source with the instructions below
(`brew install cmake zstd` provides the dependencies).

## Prerequisites

To build this project, you will need:

  - A C++17 compliant compiler (e.g., GCC, Clang, MSVC)
  - CMake (version 3.15 or newer)
  - **Zlib** library (development headers)
  - **Zstandard (zstd)** library (development headers)

## Building

Prebuilt binaries for Windows, Linux and Android are built by GitHub Actions and attached to every tagged release on the [Releases](https://github.com/Fer1d/KDZTool/releases) page.

The project can be built using a standard C++ compiler and CMake. For example, under Linux distros, you can compile with a command like this:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Building for Android

The tool is plain C++17 plus zlib/zstd, so it builds for Android in two ways.

**On the device itself (Termux, no cross-compiling):**

```bash
pkg install clang cmake ninja pkg-config zlib zstd git
git clone https://github.com/Fer1d/KDZTool.git
cd KDZTool
termux-setup-storage                      # grant access to /sdcard
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/kdz-tool extract /sdcard/Download/fw.kdz -d /sdcard/Download/out --rawprogram
```

Termux ships `zlib.pc` and `libzstd.pc`, which is exactly what the build looks for
outside Windows, and the clang it installs is an ordinary C++17 compiler.

**Cross-compiling with the Android NDK (arm64-v8a shown):**

The NDK sysroot already provides zlib, but zstd has to be built for the target first:

```bash
export NDK=$HOME/android-ndk-r27
export TOOLCHAIN=$NDK/build/cmake/android.toolchain.cmake
export ABI=arm64-v8a
export PREFIX=$PWD/android-prefix

# 1) zstd for Android (static library plus its pkg-config file)
git clone --depth 1 https://github.com/facebook/zstd.git
cmake -S zstd/build/cmake -B zstd-build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 \
      -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_BUILD_TYPE=Release \
      -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_SHARED=OFF
cmake --build zstd-build -j && cmake --install zstd-build

# 2) point pkg-config at that zstd, then build this tool (no CMakeLists change needed)
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=/
cmake -S . -B build-android -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-24 \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-android -j

# 3) run it on the device
adb push build-android/kdz-tool /data/local/tmp/
adb shell chmod +x /data/local/tmp/kdz-tool
adb shell /data/local/tmp/kdz-tool extract /sdcard/Download/fw.kdz -d /data/local/tmp/out --rawprogram
```

Notes:

  - Use `-DANDROID_ABI=x86_64` for an emulator and `armeabi-v7a` for 32-bit devices.
  - `-DANDROID_PLATFORM=android-24` is what `std::filesystem` and `timegm` need; older levels may fail to compile.
  - The tool has to read and write firmware, so run it somewhere writable: `/data/local/tmp` (adb push/pull) or a Termux home directory.
  - Android without root cannot flash anything: this tool only unpacks and repacks. Flashing still needs QFIL, LGUP or similar.

## Usage

The tool is operated via the command line with two main commands: `extract` and `repack`. The examples below use `kdz-tool`; replace it with the file you downloaded (see [Getting the binaries](#getting-the-binaries)).

```
A tool to extract and repack LG KDZ firmware.
Usage: ./kdz-tool <command> [options]

Commands:
  extract    Extract a KDZ file to a folder.
  repack     Repack an extracted folder into a KDZ file.

General Options:
  -h, --help           Show this help message and exit.
```

### Extracting a KDZ

This command parses a KDZ file and extracts its contents into a specified directory. If no directory is provided, it will only print the header information without writing any files.

**Syntax:**

```
./kdz-tool extract <kdz_file> [-d <path>] [--no-verify] [--strict] [--rawprogram] [--keep-b] [--sector-size <bytes>]
```

  - `<kdz_file>`: Path to the input KDZ firmware file.
  - `-d, --dest <path>`: The directory to extract files to.
  - `--no-verify`: (Optional) Skip the full DZ data hash verification for a faster initial parse. Useful for quick inspection.
  - `--strict`: (Optional) Treat consistency problems as hard errors again: checksum mismatches, unexpected field values, a `part_start_sector` that disagrees with the partition layout, and so on.
  - `--rawprogram`: (Optional) Also write `rawprogram<N>.xml` and `patch<N>.xml` next to the partition images, so the folder can be flashed in 9008/EDL mode with QFIL. Requires `-d`.
  - `--keep-b`: (Optional) With `--rawprogram`: extract the B slots of an A/B device as well, instead of flashing them from the image of the A slot.
  - `--sector-size <bytes>`: (Optional) Override the detected sector size. Must be a power of two between 512 and 65536.

#### Consistency warnings vs. real errors

By default the parser only stops on structural damage - a truncated file, a wrong magic number, or a compression scheme the tool has no decompressor for. Everything that merely disagrees with a value the tool *derived* from the file (header CRC32, the MD5 of the chunk headers and of the data, the build weekday, fields that are only expected to be zero, `part_start_sector`) is reported as a warning instead of aborting the parse, and the value stored in the file always wins. All findings are listed in the `diagnostics` array of `metadata.json`, and `--strict` restores the old abort-on-first-mismatch behaviour.

#### Building a 9008/EDL (QFIL) flash package

With `--rawprogram` the extract command also writes Qualcomm compatible metadata next to the partition images:

  - `rawprogram<N>.xml`: one `<program>` element per run of data of physical partition `N` (LUN), with its start sector, sector count, size and `SECTOR_SIZE_IN_BYTES`.
  - A partition is only split into one small file per chunk (named after the chunk) when that leaves out more than 32 MiB of empty space: userdata (6 MiB of data in a 10 GiB partition) and `system_b` are split, a 4 MiB partition such as `oem_a` stays a single `oem_a.img`. Everything else becomes one `<partition>.img`. That is the layout LG's own packages use, and it keeps a 9008 package at the size of the data instead of the size of the partitions.
  - File names carry no index: LUN 0 is written without a prefix (`system_a.img`, `userdata_1.img_3702662`), the other LUNs get the letter the reference tools use (`B.` for LUN 1, `C.` for LUN 2, ...).
  - The B slot of an A/B device is not extracted at all: its entries flash the image of the A slot (`system_b` writes `system_a.img` at the position of `system_b`), which is what LG's own packages do. When the data the DZ stores for a B slot differs from its A slot the run says so, and `--keep-b` extracts the B slots normally instead.
  - A partition whose chunk metadata (relative offsets, sizes and the MD5 of every chunk) proves it holds the same bytes as another one is not extracted twice either: `xbl_b` on LUN 2 flashes `B.xbl_a.img` of LUN 1.
  - Whole-partition images that are still byte-identical after that are shared as well.
  - An entry that ends exactly at the end of the disk (the backup GPT) is placed with the `NUM_DISK_SECTORS` placeholder so that it follows the device when it offers more capacity; every other entry keeps fixed sectors.
  - `patch<N>.xml`: entries that resize the partition which grows to fill the disk. Values that depend on the target capacity use the `NUM_DISK_SECTORS` placeholder, which QFIL substitutes while flashing. When no partition grows, the file only contains an empty `<patches>` element.

Because a sparse partition is stored as chunk files, the whole-partition images are only written by a plain
`extract` (without `--rawprogram`) - which is also the layout `repack` needs.

The sector size is read from the GPT embedded in the firmware. LG devices only use 512-byte (eMMC) and 4096-byte (UFS) logical blocks, so those are the two sizes the probe tries, starting with the one implied by the `is_ufs` flag of the DZ header; a GPT that contradicts that flag, or a chunk size heuristic that disagrees with the GPT, is recorded as a warning. When no GPT is found the heuristic is used instead, and `--sector-size` overrides everything - it is also the way to force any other sector size. A complete QFIL flash still needs the matching firehose programmer (`prog_*.mbn`), which is not part of a KDZ file.

One note on `sparse`: the `is_sparse` flag of a DZ chunk header does not mean the payload is an Android sparse image - it marks a partition that the archive stores with holes. The generator therefore inspects the image file itself and only sets `sparse="true"` when the Android sparse magic is really present; such an entry then describes the expanded partition.

**Example:**

```bash
./kdz-tool extract G850UM20A_00_NAO_US_OP_0416.kdz -d G850_extracted
```

After extraction, the output directory will have the following structure:

```
G850_extracted/
├── 0.abl.img
├── 0.aop.img
├── 0.boot.img
├── ... (other partition images)
├── components/
│   ├── LGE_COMMON.dll
│   ├── LGE_VER.dll
│   ├── suffix_map.dat
│   └── ... (other components)
└── metadata.json
```

### Repacking a Directory

This command rebuilds a KDZ file from an extracted directory containing partition images, components, and a `metadata.json` file.

**Syntax:**

```
./kdz-tool repack <input_dir> <output_file>
```

  - `<input_dir>`: Path to the directory containing extracted files and `metadata.json`.
  - `<output_file>`: Path for the new output KDZ file to be created.

**Example:**

```bash
./kdz-tool repack G850_extracted my_custom_firmware.kdz
```

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

This tool relies on several excellent open-source libraries:

  - [**nlohmann/json**](https://github.com/nlohmann/json): For easy and robust JSON parsing and serialization.
  - [**zlib**](https://www.zlib.net/): For handling `zlib` compression.
  - [**Zstandard (zstd)**](https://facebook.github.io/zstd/): For handling `zstd` compression.
  - The MD5 implementation is based on the work of **bzflag**, available at [www.zedwood.com](http://www.zedwood.com/article/cpp-md5-function).

-----

## Appendix: Detailed Header Structure and Field Analysis

### KDZ File Structure (`KdzHeader`)

#### KDZ Header (V1 Version)
Total Size: 1304 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1304 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x50447932` |
| `dz_record` | 8 | 264 | Struct | DZ file record (see table below) |
| `dll_record` | 272 | 264 | Struct | DLL file record (see table below) |
| `padding` | 536 | 768 | Byte Array | Zero-padding |

**V1 Record Structure (`V1_RECORD_FMT`)**
Size: 264 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | File name (null-terminated ASCII string) |
| `size` | 4 | Unsigned Int | File size |
| `offset` | 4 | Unsigned Int | Starting offset of the file within the KDZ |

---

#### KDZ Header (V2 Version)
Total Size: 1320 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1320 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x80253134` |
| `dz_record` | 8 | 272 | Struct | DZ file record (see table below) |
| `dll_record` | 280 | 272 | Struct | DLL file record (see table below) |
| `marker` | 552 | 1 | Byte | Marker byte, usually `0x00` or `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib file record (see table below) |
| `unknown_record`| 825 | 272 | Struct | Unknown record (usually empty) (see table below) |
| `padding` | 1097 | 223 | Byte Array | Zero-padding |

**V2/V3 Record Structure (`V2_RECORD_FMT`)**
Size: 272 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 256 | Char Array | File name (null-terminated ASCII string) |
| `size` | 8 | Unsigned Long | File size (upgraded to 64-bit to support >4GB files) |
| `offset` | 8 | Unsigned Long | Starting offset of the file within the KDZ (upgraded to 64-bit) |

---

#### KDZ Header (V3 Version)
Total Size: 1320 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `size` | 0 | 4 | Unsigned Int | Header size, fixed at 1320 |
| `magic` | 4 | 4 | Unsigned Int | Magic number, fixed at `0x25223824` |
| `dz_record` | 8 | 272 | Struct | DZ file record (structure same as V2) |
| `dll_record` | 280 | 272 | Struct | DLL file record (structure same as V2) |
| `marker` | 552 | 1 | Byte | Marker byte, usually `0x00` or `0x03` |
| `dylib_record` | 553 | 272 | Struct | dylib file record (structure same as V2) |
| `unknown_record`| 825 | 272 | Struct | Unknown record (usually empty) (structure same as V2) |
| `extended_mem_id_size` | 1097 | 4 | Unsigned Int | Size of extended memory ID |
| `tag` | 1101 | 5 | Char Array | Tag information (e.g., "5.19") |
| `additional_records_size`| 1106 | 8 | Unsigned Long | Total size of all additional records |
| `suffix_map_offset` | 1114 | 8 | Unsigned Long | Offset of the Suffix Map |
| `suffix_map_size` | 1122 | 4 | Unsigned Int | Size of the Suffix Map |
| `sku_map_offset` | 1126 | 8 | Unsigned Long | Offset of the SKU Map |
| `sku_map_size` | 1134 | 4 | Unsigned Int | Size of the SKU Map |
| `ftm_model_name`| 1138 | 32 | Char Array | FTM (Factory Test Mode) model name |
| `extended_sku_map_offset`| 1170 | 8 | Unsigned Long | Offset of the Extended SKU Map |
| `extended_sku_map_size` | 1178 | 4 | Unsigned Int | Size of the Extended SKU Map |
| `padding` | 1182 | 138 | Byte Array | Zero-padding |

---

### Secure Partition Structure (`SecurePartition`)
Total Size: 82448 Bytes, Fixed Offset: 1320

#### Secure Partition Header
Size: 528 Bytes

| Field Name | Starting Offset | Size | Data Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `magic` | 0 | 4 | Unsigned Int | Magic number, fixed at `0x53430799` |
| `flags` | 4 | 4 | Unsigned Int | Flag bits |
| `part_count` | 8 | 4 | Unsigned Int | Total number of partition records |
| `sig_size` | 12 | 4 | Unsigned Int | Size of the signature |
| `signature` | 16 | 512 | Byte Array | Secure signature (maximum 512 bytes) |

#### Secure Partition Record Structure
Size: 80 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `name` | 30 | Char Array | Partition name (null-terminated ASCII string) |
| `hw_part` | 1 | Byte | Hardware partition number |
| `logical_part` | 1 | Byte | Logical partition number |
| `start_sect` | 4 | Unsigned Int | Starting sector |
| `end_sect` | 4 | Unsigned Int | Ending sector |
| `data_sect_cnt` | 4 | Unsigned Int | Number of sectors occupied by data |
| `reserved` | 4 | Unsigned Int | Reserved field, should be 0 |
| `hash` | 32 | Byte Array | Checksum of the partition (SHA-256) |

---

### DZ File Structure (`DzHeader`)

#### DZ Main Header (`HDR_FMT`)
Total Size: 512 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | DZ header magic number, fixed at `0x74189632` |
| `major` | 4 | Unsigned Int | DZ format major version number |
| `minor` | 4 | Unsigned Int | DZ format minor version number |
| `reserved` | 4 | Unsigned Int | Reserved field, should be 0 |
| `model_name` | 32 | Char Array | Device model name |
| `sw_version` | 128 | Char Array | Software version number |
| `build_date` | 16 | 8x Unsigned Short | Build date (Year/Month/Week/Day/Hour/Minute/Second/Millisecond) |
| `part_count` | 4 | Unsigned Int | Total number of partition chunks |
| `chunk_hdrs_hash`| 16 | Byte Array | MD5 checksum of all chunk headers |
| `secure_image_type`| 1 | Byte | Secure image type |
| `compression` | 9 | Char Array / Byte | Compression type ('zlib'/'zstd' or 1/4) |
| `data_hash` | 16 | Byte Array | MD5 checksum of all data |
| `swfv` | 50 | Char Array | Software Firmware Version (SWFV) |
| `build_type` | 16 | Char Array | Build type (USER/DEBUG) |
| `unknown_0` | 4 | Unsigned Int | Unknown field, should be 0 |
| `header_crc` | 4 | Unsigned Int | CRC32 checksum of the DZ main header |
| `android_ver` | 10 | Char Array | Android version number |
| `memory_size` | 11 | Char Array | Memory size |
| `signed_security` | 4 | Char Array | Whether it's a secure signature ('Y' or 'N') |
| `is_ufs` | 4 | Unsigned Int | Whether it's UFS storage (non-zero is yes) |
| `anti_rollback_ver`| 4 | Unsigned Int | Anti-rollback version number |
| `supported_mem`| 64 | Char Array | List of supported memory types |
| `target_product` | 24 | Char Array | Target product name |
| `multi_panel_mask` | 1 | Byte | Bitmask for multi-panel support |
| `product_fuse_id` | 1 | Byte | Product fuse ID (ASCII digit or byte from 0-9) |
| `unknown_1` | 4 | Unsigned Int | Unknown field, should be 0 or `0xFFFFFFFF` |
| `is_factory_image` | 1 | Byte | Whether it's factory firmware (ASCII 'F' means yes) |
| `operator_code` | 24 | Char Array | Operator code |
| `unknown_2` | 4 | Unsigned Int | Unknown field, should be 0 or 1 |
| `padding` | 44 | Byte Array | Zero-padding |

#### DZ Chunk Header (V0 Version)
Total Size: 124 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | Chunk magic number, fixed at `0x78951230` |
| `part_name` | 32 | Char Array | Name of the parent partition |
| `chunk_name` | 64 | Char Array | Name of the chunk |
| `decompressed_size`| 4 | Unsigned Int | Size after decompression |
| `compressed_size` | 4 | Unsigned Int | Size after compression |
| `hash` | 16 | Byte Array | MD5 checksum of the chunk data |

#### DZ Chunk Header (V1 Version)
Total Size: 512 Bytes

| Field Name | Size | Data Type | Description |
| :--- | :--- | :--- | :--- |
| `magic` | 4 | Unsigned Int | Chunk magic number, fixed at `0x78951230` |
| `part_name` | 32 | Char Array | Name of the parent partition |
| `chunk_name` | 64 | Char Array | Name of the chunk |
| `decompressed_size`| 4 | Unsigned Int | Size after decompression |
| `compressed_size` | 4 | Unsigned Int | Size after compression |
| `hash` | 16 | Byte Array | MD5 checksum of the chunk data |
| `start_sector` | 4 | Unsigned Int | Starting sector on the device |
| `sector_count` | 4 | Unsigned Int | Number of sectors occupied |
| `hw_partition` | 4 | Unsigned Int | Hardware partition number |
| `crc` | 4 | Unsigned Int | CRC32 checksum of the chunk |
| `unique_part_id` | 4 | Unsigned Int | Unique partition ID |
| `is_sparse` | 4 | Unsigned Int | Whether it's a Sparse Image |
| `is_ubi_image` | 4 | Unsigned Int | Whether it's a UBI Image |
| `part_start_sector`| 4 | Unsigned Int | Starting sector of the entire partition |
| `padding` | 356 | Byte Array | Zero-padding |

### Note
* All multi-byte integer types (such as short, int, long) are stored in **Little-endian** format.
* All size units are in **Bytes**.