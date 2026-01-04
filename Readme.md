# Dolphin - 联机对战功能优化

  请直接使用dev-2509分支;
  这是一个专门为联机相关功能添加功能的版本, 目前已经支持:

通用功能:
  1. 自定义键位label:你可以为你的游戏添加每个按键对应的功能的模板;
  2. 自动添加镜像路径:现在, 模拟器会自动添加同等级的iso_files作为默认路径, 这是为了方便新手的第一次使用;
  3. 在中国, wii遥控器几乎未被使用, 所以现在点击主界面的控制器按钮, 会直接弹出1P ~ 4P给玩家选择, 并跳转到"标准控制器"设置界面, 这是目前为止我了解到的最常见的键位设置界面;
  4. 现在, 存档会根据镜像文件的ID和哈希值共同决定, 而不是单纯的ID, 这样, 不同Mod版本的同个ID游戏也可以独享自己的存档进度;
  5. 主界面添加联机快捷输入框, 以及联机大厅(Netplay Hall)
   
联机相关:
1. 现在被分为两个版本, 纯服务器版本和客户端版本
2. 服务器版本:
   1. 服务器版本作为主机使用的时候, 不再进入游戏, 而仅仅作为数据传输的桥梁; 在一台低配置的云服务器上面(例如2H2G), 也可以开几个房间, 只需要设置端口不同即可;
   2. 由于不再需要进入游戏, 现在服务器版本不再需要游戏镜像文件;
   3. 现在服务器版本支持任意Wii游戏(暂时未测试其他游戏), 客户端修改游戏的时候会把相关游戏信息上传给服务器;
   4. 由于服务器版本的主机已经不再进入游戏, 所以存档上传需要借助客户端, 现在客户端如果想要保存本次游玩的进度, 需要点击联机窗口的"上传文档"按钮;
   5. 为了防止部分客户端在服务器内闲置, 现在不进入游戏并且超过15分支的客户端会被踢出房间
3. 客户端版本:
   1. 现在客户端版本连接进入房间后, 拥有"开始游戏"、"调节缓冲"、"更换游戏"、"更换玩家映射"权限, 具备上传存档功能;
4. 联机新功能:
   1. 游戏中途可以直接更换玩家映射, 例如1P和2P互换, 或者把一个只能观战的映射为none的玩家调为1P或者2P等, 总之, 如果多个朋友一起玩对战游戏(例如BT3), 不再需要关闭游戏并重新打开来更换映射;
   2. 游戏可以从某个即时存档状态直接开始, 只要所有的客户端在Dolphin Emulation/StateSaves/initial/路径上放置同个即时存档文件, 并按照特定的格式命名, 游戏开始后所有人都会从这个即时存档状态开始游戏, 不再需要等待漫长的开始动画, 例如原版的日版BT3镜像: RDSJAF_531c9777.sav, 其中RDSJAF是游戏ID, 531c9777则是文件哈希值的前八位, 关于这点后续会补充按钮, 让用户直接保存到目标文件夹, 无须自己手动操作;
   3. 现在, 游戏中途也可以加入新的玩家! 玩家只需要在联机窗口点击"等待加入", 新玩家即可连接进入房间; 这个时候所有人的游戏会被暂停, 并且新玩家会接收服务器发送的即时存档, 速度取决于服务器带宽

总结:
   开发初衷是让新玩家可以更加简单的上手模拟器, 以及联机功能; 目前依旧正在开发中, 并且由于我是独自开发, 许多新功能没有经过严格的测试, 而且很多地方的实现应该采用更好的方法, 目前大部分代码是通过Trae实现的.

# Dolphin - NetPlay Optimization

This is a specialized fork of the Dolphin Emulator, specifically enhanced for an improved **NetPlay** (online multiplayer) experience.

---

## 🚀 General Features

* **Custom Key Labels**: Create templates to label specific button functions for each game.
* **Automatic Image Path**: The emulator automatically sets the `iso_files` directory as the default path for easier initial setup.
* **Streamlined Controller Setup**: Clicking the "Controllers" button now directly opens the **Standard Controller** settings for Players 1-4, skipping unnecessary Wii Remote menus.
* **Hash-Based Save Management**: Save files are now identified by both **Game ID** and **Hash Value**. This allows different Mod versions of the same game to have independent save data.
* **UI Enhancements**: Added a quick-input box for NetPlay and a dedicated **NetPlay Lobby** to the main interface.

---

## 🌐 NetPlay Enhancements

The project is now divided into two versions: **Server** (Headless) and **Client**.

### 1. Server Version (Host)
* **Bridge Mode**: When hosting, the server acts solely as a data bridge and does not render the game. This allows low-spec cloud servers (e.g., **2C2G**) to host multiple rooms on different ports.
* **No ISO Required**: The server version does not require game image files to run.
* **Universal Wii Support**: (Beta) Supports various Wii games; clients will sync game information to the server automatically.
* **Client-Assisted Saves**: Since the host doesn't run the game, players can use the **"Upload Save"** button in the NetPlay window to sync progress back to the server.
* **Anti-Idle System**: To optimize server resources, idle clients (not in-game) will be kicked after **15 minutes**.

### 2. Client Version
* **Enhanced Permissions**: Connected clients can now **Start Game**, **Adjust Buffer**, **Change Game**, and **Remap Players**.

---

## 🆕 Advanced Functions

* **Mid-Game Remapping**: Swap player slots (e.g., 1P and 2P) or change an observer to a player **during the session** without restarting.
* **Instant Start (Savestate Sync)**: Skip intro animations by booting directly from a savestate. 
    * Place identical files in `Dolphin Emulation/StateSaves/initial/`.
    * Format: `GameID_Hash.sav` (e.g., `RDSJAF_531c9777.sav`).
* **Late-Join Support**: New players can join mid-session! The game will pause while the new player receives the current **Savestate** from the server. (Transmission speed depends on server bandwidth).

---

## 📝 Note from Developer

This project aims to lower the barrier for new players to enjoy Dolphin's NetPlay. It is currently a **Work In Progress (WIP)** and developed solely by me. Many features are still in testing. 

> **Implementation Note**: Significant portions of the code logic were implemented with the assistance of **Trae** (AI). I am actively working on optimizing the codebase for better performance and stability.

# Dolphin - A GameCube and Wii Emulator

[Homepage](https://dolphin-emu.org/) | [Project Site](https://github.com/dolphin-emu/dolphin) | [Buildbot](https://dolphin.ci/) | [Forums](https://forums.dolphin-emu.org/) | [Wiki](https://wiki.dolphin-emu.org/) | [GitHub Wiki](https://github.com/dolphin-emu/dolphin/wiki) | [Issue Tracker](https://bugs.dolphin-emu.org/projects/emulator/issues) | [Coding Style](https://github.com/dolphin-emu/dolphin/blob/master/Contributing.md) | [Transifex Page](https://app.transifex.com/dolphinemu/dolphin-emu/dashboard/) | [Analytics](https://mon.dolphin-emu.org/)

Dolphin is an emulator for running GameCube and Wii games on Windows,
Linux, macOS, and recent Android devices. It's licensed under the terms
of the GNU General Public License, version 2 or later (GPLv2+).

Please read the [FAQ](https://dolphin-emu.org/docs/faq/) before using Dolphin.

## System Requirements

### Desktop

* OS
    * Windows (10 1903 or higher).
    * Linux.
    * macOS (11.0 Big Sur or higher).
    * Unix-like systems other than Linux are not officially supported but might work.
* Processor
    * A CPU with SSE2 support.
    * A modern CPU (3 GHz and Dual Core, not older than 2008) is highly recommended.
* Graphics
    * A reasonably modern graphics card (Direct3D 11.1 / OpenGL 3.3).
    * A graphics card that supports Direct3D 11.1 / OpenGL 4.4 is recommended.

### Android

* OS
    * Android (5.0 Lollipop or higher).
* Processor
    * A processor with support for 64-bit applications (either ARMv8 or x86-64).
* Graphics
    * A graphics processor that supports OpenGL ES 3.0 or higher. Performance varies heavily with [driver quality](https://dolphin-emu.org/blog/2013/09/26/dolphin-emulator-and-opengl-drivers-hall-fameshame/).
    * A graphics processor that supports standard desktop OpenGL features is recommended for best performance.

Dolphin can only be installed on devices that satisfy the above requirements. Attempting to install on an unsupported device will fail and display an error message.

## Building for Windows

Use the solution file `Source/dolphin-emu.sln` to build Dolphin on Windows.
Dolphin targets the latest MSVC shipped with Visual Studio or Build Tools.
Other compilers might be able to build Dolphin on Windows but have not been
tested and are not recommended to be used. Git and latest Windows SDK must be
installed when building.

Make sure to pull submodules before building:
```sh
git submodule update --init --recursive
```

The "Release" solution configuration includes performance optimizations for the best user experience but complicates debugging Dolphin.
The "Debug" solution configuration is significantly slower, more verbose and less permissive but makes debugging Dolphin easier.

## Building for Linux and macOS

Dolphin requires [CMake](https://cmake.org/) for systems other than Windows. 
You need a recent version of GCC or Clang with decent c++20 support. CMake will
inform you if your compiler is too old.
Many libraries are bundled with Dolphin and used if they're not installed on 
your system. CMake will inform you if a bundled library is used or if you need
to install any missing packages yourself. You may refer to the [wiki](https://github.com/dolphin-emu/dolphin/wiki/Building-for-Linux) for more information.

Make sure to pull submodules before building:
```sh
git submodule update --init --recursive
```

### macOS Build Steps:

A binary supporting a single architecture can be built using the following steps: 

1. `mkdir build`
2. `cd build`
3. `cmake ..`
4. `make -j $(sysctl -n hw.logicalcpu)`

An application bundle will be created in `./Binaries`.

A script is also provided to build universal binaries supporting both x64 and ARM in the same
application bundle using the following steps:

1. `mkdir build`
2. `cd build`
3. `python ../BuildMacOSUniversalBinary.py`
4. Universal binaries will be available in the `universal` folder

Doing this is more complex as it requires installation of library dependencies for both x64 and ARM (or universal library
equivalents) and may require specifying additional arguments to point to relevant library locations. 
Execute BuildMacOSUniversalBinary.py --help for more details.  

### Linux Global Build Steps:

To install to your system.

1. `mkdir build`
2. `cd build`
3. `cmake ..`
4. `make -j $(nproc)`
5. `sudo make install`

### Linux Local Build Steps:

Useful for development as root access is not required.

1. `mkdir Build`
2. `cd Build`
3. `cmake .. -DLINUX_LOCAL_DEV=true`
4. `make -j $(nproc)`
5. `ln -s ../../Data/Sys Binaries/`

### Linux Portable Build Steps:

Can be stored on external storage and used on different Linux systems.
Or useful for having multiple distinct Dolphin setups for testing/development/TAS.

1. `mkdir Build`
2. `cd Build`
3. `cmake .. -DLINUX_LOCAL_DEV=true`
4. `make -j $(nproc)`
5. `cp -r ../Data/Sys/ Binaries/`
6. `touch Binaries/portable.txt`

## Building for Android

These instructions assume familiarity with Android development. If you do not have an
Android dev environment set up, see [AndroidSetup.md](AndroidSetup.md).

Make sure to pull submodules before building:
```sh
git submodule update --init --recursive
```

If using Android Studio, import the Gradle project located in `./Source/Android`.

Android apps are compiled using a build system called Gradle. Dolphin's native component,
however, is compiled using CMake. The Gradle script will attempt to run a CMake build
automatically while building the Java code.

## Uninstalling

On Windows, simply remove the extracted directory, unless it was installed with the NSIS installer,
in which case you can uninstall Dolphin like any other Windows application.

Linux users can run `cat install_manifest.txt | xargs -d '\n' rm` as root from the build directory
to uninstall Dolphin from their system.

macOS users can simply delete Dolphin.app to uninstall it.

Additionally, you'll want to remove the global user directory if you don't plan on reinstalling Dolphin.

## Command Line Usage

```
Usage: Dolphin.exe [options]... [FILE]...

Options:
  --version             show program's version number and exit
  -h, --help            show this help message and exit
  -u USER, --user=USER  User folder path
  -m MOVIE, --movie=MOVIE
                        Play a movie file
  -e <file>, --exec=<file>
                        Load the specified file
  -n <16-character ASCII title ID>, --nand_title=<16-character ASCII title ID>
                        Launch a NAND title
  -C <System>.<Section>.<Key>=<Value>, --config=<System>.<Section>.<Key>=<Value>
                        Set a configuration option
  -s <file>, --save_state=<file>
                        Load the initial save state
  -d, --debugger        Show the debugger pane and additional View menu options
  -l, --logger          Open the logger
  -b, --batch           Run Dolphin without the user interface (Requires
                        --exec or --nand-title)
  -c, --confirm         Set Confirm on Stop
  -v VIDEO_BACKEND, --video_backend=VIDEO_BACKEND
                        Specify a video backend
  -a AUDIO_EMULATION, --audio_emulation=AUDIO_EMULATION
                        Choose audio emulation from [HLE|LLE]
```

Available DSP emulation engines are HLE (High Level Emulation) and
LLE (Low Level Emulation). HLE is faster but less accurate whereas
LLE is slower but close to perfect. Note that LLE has two submodes (Interpreter and Recompiler)
but they cannot be selected from the command line.

Available video backends are "D3D" and "D3D12" (they are only available on Windows), "OGL", and "Vulkan".
There's also "Null", which will not render anything, and
"Software Renderer", which uses the CPU for rendering and
is intended for debugging purposes only.

## DolphinTool Usage
```
usage: dolphin-tool COMMAND -h

commands supported: [convert, verify, header, extract]
```

```
Usage: convert [options]... [FILE]...

Options:
  -h, --help            show this help message and exit
  -u USER, --user=USER  User folder path, required for temporary processing
                        files.Will be automatically created if this option is
                        not set.
  -i FILE, --input=FILE
                        Path to disc image FILE.
  -o FILE, --output=FILE
                        Path to the destination FILE.
  -f FORMAT, --format=FORMAT
                        Container format to use. Default is RVZ. [iso|gcz|wia|rvz]
  -s, --scrub           Scrub junk data as part of conversion.
  -b BLOCK_SIZE, --block_size=BLOCK_SIZE
                        Block size for GCZ/WIA/RVZ formats, as an integer.
                        Suggested value for RVZ: 131072 (128 KiB)
  -c COMPRESSION, --compression=COMPRESSION
                        Compression method to use when converting to WIA/RVZ.
                        Suggested value for RVZ: zstd [none|zstd|bzip|lzma|lzma2]
  -l COMPRESSION_LEVEL, --compression_level=COMPRESSION_LEVEL
                        Level of compression for the selected method. Ignored
                        if 'none'. Suggested value for zstd: 5
```

```
Usage: verify [options]...

Options:
  -h, --help            show this help message and exit
  -u USER, --user=USER  User folder path, required for temporary processing
                        files.Will be automatically created if this option is
                        not set.
  -i FILE, --input=FILE
                        Path to disc image FILE.
  -a ALGORITHM, --algorithm=ALGORITHM
                        Optional. Compute and print the digest using the
                        selected algorithm, then exit. [crc32|md5|sha1|rchash]
```

```
Usage: header [options]...

Options:
  -h, --help            show this help message and exit
  -i FILE, --input=FILE
                        Path to disc image FILE.
  -b, --block_size      Optional. Print the block size of GCZ/WIA/RVZ formats,
then exit.
  -c, --compression     Optional. Print the compression method of GCZ/WIA/RVZ
                        formats, then exit.
  -l, --compression_level
                        Optional. Print the level of compression for WIA/RVZ
                        formats, then exit.
```

```
Usage: extract [options]...

Options:
  -h, --help            show this help message and exit
  -i FILE, --input=FILE
                        Path to disc image FILE.
  -o FOLDER, --output=FOLDER
                        Path to the destination FOLDER.
  -p PARTITION, --partition=PARTITION
                        Which specific partition you want to extract.
  -s SINGLE, --single=SINGLE
                        Which specific file/directory you want to extract.
  -l, --list            List all files in volume/partition. Will print the
                        directory/file specified with --single if defined.
  -q, --quiet           Mute all messages except for errors.
  -g, --gameonly        Only extracts the DATA partition.
```
