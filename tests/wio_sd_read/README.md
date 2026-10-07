# Wio Terminal SD 卡独立读取测试

本目录是独立 PlatformIO 工程，具有自己的 `src/`、依赖和 `.pio/` 编译目录，不参与 `../../firmware/` 原项目的编译。测试程序只以 `FILE_READ` 打开 SD 卡内容，不创建、修改或删除卡上的文件。

## 准备

1. 准备 microSD 卡。首次排查建议使用 16 GB 或更小的 FAT32 卡；格式化会清除数据，请先备份。Seeed 官方格式说明：https://wiki.seeedstudio.com/Wio-Terminal-FS-Overview/
2. 将本目录 `sd_card/test.txt` **文件本身**复制到 SD 卡根目录，最终路径为 `/test.txt`，不是 `/sd_card/test.txt`。也可以换成自己的非空文本文件；改文件名时同步修改 `src/main.cpp` 的 `TEST_FILE`。
3. 断电插卡，再通过 USB 连接 Wio Terminal。
4. 关闭占用设备串口的宠物宿主程序或串口监视器。下面以 COM8 为例，按实际端口修改。

## 编译、烧录和查看

在 PowerShell 中执行：

```powershell
cd D:\vibe_pet\tests\wio_sd_read
# 当前电脑的 PlatformIO 不在 PATH 中时，使用下面的绝对路径。
$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe"
& $pio run
& $pio device list
& $pio run -t upload --upload-port COM8
& $pio device monitor --port COM8 --baud 115200
```

烧录会将设备当前运行的宠物固件替换为测试固件；原项目源文件不受影响。2026-10-02 已按用户要求完成烧录及校验，随后用户确认读取测试可用，详见 [测试记录](TEST_RECORD.md)。

打开串口较晚时，发送 `r` 可重新测试（大小写均可），或按设备复位键。启动等待串口最多 3 秒，不接串口也会在屏幕显示结果。

## 测试结果

- `OK: SD mounted`：SD 卡挂载成功，串口显示卡容量。
- `OK: root directory opened`：根目录打开成功；串口最多列出 64 个条目，不递归子目录。
- 串口预览 `/test.txt` 前 256 字节；程序通过 512 字节缓冲区读取整个文件，不将整个文件装入内存。
- `Bytes: X / X` 与 `PASS: whole file read`：已读取文件声明的全部字节。
- `FNV-1a`：本次所读数据的 32 位校验值，可用来比较重复读取结果；PASS 只验证读取长度，不代表已与电脑原文件逐字节比对。
- `FAIL: SD mount failed`：检查卡是否插好及文件系统，建议用已知正常的小容量 FAT32 卡复测。
- `FAIL: cannot open test.txt`：挂载后未能打开目标文件，检查根目录文件名、路径和访问情况。
- `FAIL: test.txt is empty`：目标文件为空，无法验证有效数据读取。
- `FAIL: incomplete read`：读取在声明大小之前停止。

屏幕显示摘要，串口显示完整诊断。耗时包含串口输出，不作为纯 SD 读取速度基准。读取过程中不要拔卡；换卡请先断电。

## 恢复宠物固件

关闭串口监视器后执行：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d D:\vibe_pet\firmware -e vibe_pet -t upload --upload-port COM8
```
