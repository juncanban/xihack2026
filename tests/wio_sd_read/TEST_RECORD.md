# Wio Terminal SD 卡读取测试记录

- 日期：2026-10-02（Asia/Shanghai）
- 结论：独立测试固件编译、烧录及 Flash 校验通过；SD 卡挂载和根目录打开已通过串口验证，文件读取测试最终由用户确认可用。
- 隔离范围：所有测试代码和记录位于 `D:\vibe_pet\tests\wio_sd_read`，未修改原项目 `D:\vibe_pet\firmware` 源码。设备当前烧录的是测试固件。

## 测试配置

| 项目 | 配置 / 实测值 |
| --- | --- |
| 开发板 | Seeed Wio Terminal |
| 构建环境 | PlatformIO / Arduino，`sd_read_test` |
| 上传与串口端口 | COM8 |
| 串口波特率 | 115200 |
| SD 接口 | 板载 microSD 插槽，`SDCARD_SS_PIN` / `SDCARD_SPI`，SPI 4 MHz |
| 依赖 | Seeed Arduino FS 2.1.5；Seeed Arduino SFUD 2.0.2 |
| 卡容量 | 串口报告 14910 MiB（约 14.6 GiB） |
| 测试文件 | SD 卡根目录 `/test.txt`；工程示例为 `sd_card/test.txt` |
| 文件访问方式 | 只读；512 字节缓冲区逐块读取，串口预览前 256 字节 |

## 验证过程

1. 独立工程编译成功，生成 69536 字节固件；编译报告 RAM 使用 8724 字节。
2. 按用户要求烧录到 COM8；上传工具返回 `Verify successful` 和 `[SUCCESS]`，确认固件写入及校验成功。
3. 通过串口发送 `r` 重新测试，获得以下诊断：

   ```text
   Wio SD Read Test
   Read only / SPI 4 MHz
   OK: SD mounted
   Card capacity (MiB): 14910
   --- Root directory (up to 64 entries) ---
   OK: root directory opened
   FAIL: cannot open test.txt
   Copy test.txt to card root.
   Serial 'r' / reset: retry
   ```

4. 用户提供的设备照片显示同样的文件打开失败提示。此时 SD 卡已挂载且根目录可打开，故障定位在目标文件打开阶段。
5. 给出处理步骤：将示例 `test.txt` 放到 SD 卡根目录，核对实际文件名，断电插回后重新上电，无须重新烧录。
6. 用户随后反馈“可以了”，确认测试可用，并要求保存本记录。

## 结果与记录边界

| 验证项 | 结果 | 依据 |
| --- | --- | --- |
| 编译 | 通过 | PlatformIO 编译输出 |
| 烧录与 Flash 校验 | 通过 | 上传日志 |
| SD 卡挂载、容量识别 | 通过 | 串口实测 |
| 根目录打开 | 通过 | 串口实测及设备照片 |
| 目标文件读取 | 用户确认可用 | 最终反馈“可以了” |

最终成功运行的字节数、FNV-1a 值及耗时未重新采集，不填写推测值。程序的 `PASS: whole file read` 判定表示读取字节数与文件声明大小一致；本次未进行与电脑源文件的逐字节比对或读写性能测试。

## 复测与恢复

- 准备卡根目录的非空 `/test.txt` 后，上电自动测试；串口发送 `r` / `R` 或复位可重测。
- 预期屏幕显示 `Bytes: X / X` 和 `PASS: whole file read`，串口同时输出文件预览与校验值。
- 程序只读卡内文件，不创建、修改或删除文件；更换 SD 卡前先断电。
- 编译、烧录、串口查看及恢复宠物固件的命令见 [README](README.md)。本次记录时未执行宠物固件恢复。
