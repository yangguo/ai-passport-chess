# 构建、CI、发布与刷机

## 当前可运行的检查

```sh
python3 tools/check_docs.py
```

GitHub Actions `docs` job 执行相同检查。当前不含 ESP-IDF 工程/棋局代码，不能运行 idf.py build，也没有 firmware Release。以下流程是 M0/M1 实现任务，命令中的脚本会在该阶段加入，不能直接当作现成入口。

## M0 引入构建基线

使用 ESP-IDF **v5.5.3**，`esp32c3` target；不要从 XiaoZhi 仓库套用 IDF 6.x。下载官方 `FoloToy/ai-passport` 的锁定 SHA 到临时路径，按来源清单导入所需 BSP、许可证、CMake、manifest、sdkconfig.defaults、分区表、dependencies.lock 与验证工具依赖链。不要只复制 validate.sh 而遗漏它调用的脚本；追踪每个移植文件和修改。

将 demo app 替换为新 `main/app_main.c`，仅初始化必要功能。关闭默认 BLE，排除 Wi-Fi/audio demo；保留正确的 USB console、RGB565、24KiB LVGL pool、8MiB flash header。生成新的 dependencies.lock 并提交；`managed_components/` 禁止手改。为每次干净构建生成隔离 sdkconfig，防止旧配置忽略 defaults。

```sh
# M0/M1 后使用；需替换实际 IDF 安装路径
. /path/to/esp-idf-v5.5.3/export.sh
idf.py --version                       # 必须报告 v5.5.3
idf.py set-target esp32c3               # 首次/目标变化；会重建配置
idf.py build                           # 增量开发
idf.py size
idf.py size-components
```

## 计划的 host/firmware 验证入口

```sh
# M1 新增后使用，无 IDF 环境也可运行
cmake -S tests/host -B build/host -DCHESS_SANITIZERS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python3 tools/check_docs.py

# M0/M1 新增后：干净配置、构建、合并、布局检查与归档
./tools/validate_firmware.sh
```

`validate_firmware.sh` 契约：固定 IDF 版本；临时干净build和sdkconfig；构建成功才运行 merge；由生成的 flash_args/flasher_args.json 推导偏移，不能写死bootloader地址；验证分区MD5、边界、app fit、镜像头8MiB与esp32c3；保留同一次构建的full/app/bootloader/partition/ELF/MAP及日志，再清临时目录。输出 `build/ai-passport-chess-full.bin` 和带版本的 `dist/` 包。失败 exit非零，禁止留下貌似新版本的旧full.bin。

## CI 的阶段演进

| 阶段 | 必需 job | 证明 |
|---|---|---|
| 文档初版 | docs | 本地文档/基准结构有效 |
| M0 | docs + firmware-smoke | BSP最小app可编译，未证明能下棋 |
| M1 | docs + host + firmware | core回归/perft、sanitizer、合并镜像 |
| M2 | 上述 + ai-adapter | deadline/cancel/mapping/regression |
| v* release | 全部 gate + package | 产物对应tag且校验完整；设备验收另附 |

firmware job 使用 `espressif/idf:v5.5.3` 并在 M0 锁定实际镜像 digest；Actions 固定 commit SHA（初始文档CI已固定）。host用Linux Clang+sanitizers，macOS定期核验。PR仅读权限，不在不可信PR下使用 secrets；cache key含IDF、manifest、lock、配置。对依赖锁漂移 fail。发布 `v*` 仅在正常分支/tag且 gate 成功后允许 contents:write；未做真机验收的产物标记 prerelease。

## 发布包内容

- `ai-passport-chess-<version>-full.bin`，从0x0刷写；app-only、bootloader、partition-table。
- ELF/MAP 与构建日志、SHA256SUMS、manifest.json。
- manifest包含Git SHA/tag、dirty=false、IDF/BSP/engine SHA、分区表、芯片/Flash、构建参数、依赖版本、每个文件长度/校验和、device gate 状态。
- 重新组合分段镜像，与full相应偏移逐字节一致；padding/gap按合并规则验证；所有内容不超过8MiB。
- full映像只因有效文件与分区覆盖而存在，不强制填充整8MiB。app-only不是可从0x0刷的交付物。

## 刷机（未来有验证产物后，操作设备需要明确授权）

开发保留NVS优先分段烧录；完整合并镜像可能以padding覆盖NVS，需在发布说明告知会重置棋局。这次文档任务不连接或刷写设备。检查芯片与8MiB Flash，不使用 erase_flash 作为常规修复。

```sh
# M1 后在已构建工程中；串口替换为实际端口
idf.py -p /dev/cu.usbmodemXXXX flash monitor

# release full包；仅在核实校验和后
python -m esptool --chip esp32c3 -p /dev/cu.usbmodemXXXX -b 460800 \
  write_flash 0x0 dist/ai-passport-chess-<version>-full.bin
```

esptool版本以v5.5.3环境安装的锁定版本为准，记录版本和实际支持的子命令。浏览器可用 [AI Passport Web Flasher](https://ai-passport.folotoy.cn/tools/web-flasher/) 选择full文件与0x0地址。刷完从串口确认commit、NVS恢复/新局、屏幕和三键；上传或push成功都不等于设备通过。

本项目不自动读取旧设备Flash作备份。若用户另行授权备份，避免提交身份/密钥/原机镜像；按既有设备维护约定保存于 XiaoZhi checkout 的日期备份目录，并限制权限。
