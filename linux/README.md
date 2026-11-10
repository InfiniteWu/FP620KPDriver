# Jolimark FP-620K+ Linux CUPS 驱动

面向 Jolimark FP-620K+ 24 针针式打印机的 Linux 驱动，直接生成打印机固件使用的
ESC/P2K 图形命令，带 USB 状态回读，不依赖 foomatic 或 Ghostscript 的 `epson`
输出设备。

## 架构

```
应用 (PDF/PS/图片)
   │
   ▼
CUPS  pdftoraster → CUPS raster (1-bit, 180×180 / 360×180 / 360×360)
   │
   ▼
rastertofp620kp        24 行/带，默认单向；360×360 为 48 行、两遍交错
   │
   ▼
fp620kp backend        写 /dev/usb/lp0，轮询 LPGETSTATUS
   │
   ▼
USB Printer Class (usblp)
```

| 组件 | 路径 | 作用 |
| --- | --- | --- |
| `src/rastertofp620kp.c` | `/usr/lib/cups/filter/rastertofp620kp` | CUPS raster → ESC/P2K |
| `src/fp620kp-backend.c` | `/usr/lib/cups/backend/fp620kp` | USB 传输 + 状态回读 |
| `jolimark-fp620kp.ppd` | `/usr/share/cups/model/` | 纸型、分辨率、默认选项 |

后端必须以 `0700 root:root` 安装。CUPS 只对“非全局可执行”的后端使用 root 权限，
这样才能打开 `/dev/usb/lp0` 并调用 `LPGETSTATUS`。

## 硬件与协议

| 项目 | 结果 |
| --- | --- |
| USB ID | `1ba0:112e` Jolimark USBPrinter |
| USB 接口 | Printer Class 7 / SubClass 1 / Protocol 2（双向） |
| 内核驱动 | `usblp`，设备节点 `/dev/usb/lp0` |
| IEEE-1284 ID | `MFG:Jolimark;CMD:ESC/P2K;MDL:FP-620K+;...` |
| 打印语言 | ESC/P2K（ESC/P2 超集，24 针） |
| 图形命令 | `ESC *` 模式 `0x27`(39)=180×180、`0x28`(40)=360 dpi 横向 |
| 360×180 | 同一 `ESC * 0x28`，24 行/带、单遍，横向 360 dpi、纵向 180 dpi |
| 360×360 | 同一 `ESC * 0x28` 分两遍交错，`ESC ( v` 以 1/360 英寸进纸 |
| 打印方向 | `ESC U 1`=单向、`ESC U 0`=双向；默认单向 |
| 图形数据 | 每列 3 字节，对应 24 根针，MSB 在上 |
| 进纸 | `ESC J n`（1/180 英寸）、`ESC ( v`（1/360 英寸）、`CR`、`FF` |

Windows 驱动 `FP620KP.GPD` 中 180×180 dpi 的 `CmdSendBlockData` 就是
`<1B>*'`，与本驱动生成的数据一致；未发现私有压缩或加密格式。

## 构建与安装

依赖：

```bash
sudo apt install build-essential libcups2-dev cups cups-filters ghostscript \
    dpkg-dev debhelper
```

安装（会提示输入 sudo 密码）：

```bash
cd linux
./install.sh              # 安装并创建队列 FP620K
./install.sh --test       # 安装后立即打印校准页
```

脚本会编译并安装 filter/backend/PPD，然后把队列指向 `fp620kp:/dev/usb/lp0`。
可用环境变量覆盖队列名和设备节点：

```bash
PRINTER_NAME=FP620K2 FP620K_DEVICE=/dev/usb/lp1 ./install.sh
```

卸载：

```bash
./install.sh --uninstall
```

## Debian / Ubuntu 安装包

仓库提供 `.deb` 安装包，适合 Ubuntu/Debian 用户：

```bash
sudo apt install ./printer-driver-fp620kp_0.1.0_amd64.deb
sudo fp620kp-setup                 # 创建或更新 FP620K 队列
sudo fp620kp-setup --default --test  # 设为默认并打印校准页
```

`fp620kp-setup` 默认查找 `/dev/usb/lp0`，也可以用 `--device /dev/usb/lp1`
指定其他节点。包安装驱动和 PPD，但不会自动修改打印队列，避免升级或卸载时
破坏已有配置。

卸载：

```bash
sudo apt remove printer-driver-fp620kp
sudo lpadmin -x FP620K             # 如不再需要该队列
```

## 发布

在 `linux/` 下执行：

```bash
make release
```

会在仓库根目录的 `release/` 生成：

- `printer-driver-fp620kp_<version>_<arch>.deb`
- `fp620kp-linux-<version>.tar.gz`
- `SHA256SUMS`
- `.buildinfo` / `.changes`

`.deb` 里的二进制依赖构建环境。比如在 Ubuntu 26.04 上构建会依赖
`libcups2t64`，不能直接装到仍使用 `libcups2` 的 Ubuntu 24.04。要给较旧的
Ubuntu/Debian 使用，请在目标发行版的容器或虚拟机里构建，或者让对方用源码包。

也可以只构建 Debian 包：

```bash
make deb
```

发布前请把 `debian/control` 和 `debian/changelog` 中的维护者姓名/邮箱改成
自己的；版本号同时写在 `debian/changelog` 和 `release.sh` 的默认值里。源码
使用 MIT 许可证，见 `LICENSE`。

## 打印

```bash
lp -d FP620K 文件.pdf                                  # 默认 360×180、单向
lp -d FP620K -o Resolution=180x180dpi 文件.pdf         # 最快，草稿
lp -d FP620K -o Resolution=360x360dpi 文件.pdf         # 两遍交错，最慢
lp -d FP620K -o PrintDirection=Bidirectional 文件.pdf  # 双向，速度略快
lpstat -p FP620K -l
```

默认 A4、360×180 dpi、单向、1-bit 黑白。PDF 中的中文会在主机端由 Ghostscript
光栅化，打印机不需要中文字库。

## 打印方向与质量

FP-620K+ 自身的“打印方向”设置默认为 `AUTO`。厂商 GPD 虽然定义了
`CmdUniDirectionOn/Off`（`ESC U 01/00`），但从未实际发送；旧版驱动也未发送
`ESC U`，于是打印机回落到双向打印。双向模式下，从左到右和从右到左的落点相差
约 1 mm，竖线和文字会在相邻打印带之间左右交错，看起来像“锯齿”或“重影”。

filter 在每页开始时发送 `ESC U 01`，PPD 暴露为：

| 选项 | 行为 | 相对速度 |
| --- | --- | --- |
| `PrintDirection=Unidirectional`（默认） | 每遍都从左到右，竖线笔直 | 100% |
| `PrintDirection=Bidirectional` | 左右交替，落点差约 1 mm | 约快 9% |

整页图形校准页实测：单向 76 秒，双向 70 秒。按“质量优先”原则默认单向。
如果打印机本体被厂商工具持久设为 `0=双向`，`ESC U 01` 可能被忽略；此时应把
本体设置改为 `1=单向` 或恢复为 `2=自动`。

队列默认值可用 `lpadmin` 持久化（重启 CUPS 后仍有效）：

```bash
lpadmin -p FP620K -o Resolution=360x180dpi -o PrintDirection=Unidirectional
lpoptions -p FP620K -l        # 带 * 的即当前默认值
```

## 分辨率与实测

| 模式 | 每带行数 | 扫描遍数 | 校准页实测 | 评价 |
| --- | --- | --- | --- | --- |
| `360x180dpi`（默认） | 24 | 1 | 109 秒 | 横向 360 dpi，文字明显比 180 清晰，竖线笔直，无加粗/重影，几何与 180 页一致 |
| `180x180dpi` | 24 | 1 | 76 秒 | 最快；文字边缘较粗，适合草稿 |
| `360x360dpi` | 48 | 2 | 212 秒 | 纵向 360 dpi，但两遍点重叠使笔画偏粗，仅作可选高密度模式 |

360×180 是当前推荐的“清晰文字”档，并已设为 PPD 和队列默认值。360×360 的
两遍交错只是把两个方向的落点叠加，点径重叠会加粗笔画，并不是真正的高分辨率
矢量渲染；需要更快时选 180×180。

## 几何校准

PPD 的可打印区从纸边 18 pt（左/右）和 36 pt（上/下）开始。打印头的机械原点与
可打印区并不重合，filter 用两个常量描述机械原点相对纸边的位置：

| 常量 | 默认值 | 含义 |
| --- | --- | --- |
| `DEFAULT_ORIGIN_X_PT` | 9.8 pt | 机械原点距纸张左边 |
| `DEFAULT_ORIGIN_Y_PT` | 11.6 pt | 机械原点距纸张上边 |

这两个值来自实机校准：标尺在 0 时，最初按 Ghostscript 参考值（24 pt）打印的中心
十字偏左 5 mm；把原点改为 9.8 pt 后整幅内容右移约 4.94 mm，同时不再裁掉最左侧
15 列光栅。360×180 模式下同样的原点换算为 41 个横向点、61 个纵向点；
360×360 模式为 41 个横向点、122 个纵向点。

需要现场微调时不必重新编译，可直接传作业选项（单位：pt，72 pt = 1 英寸）：

```bash
lp -d FP620K -o fp620k-origin-x=9.8 -o fp620k-origin-y=11.6 文件.pdf
```

- 增大 `origin-x`：图像左移；减小则右移。
- 增大 `origin-y`：图像上移；减小则下移。

打印机本身的手动纸张位置标尺应保持在 0，驱动会按机械原点补偿。若你更习惯用标尺
补偿，也可以把标尺调到合适位置后不再改动作业选项。

校准页原稿和预览：

- `tests/calibration.ps`：PostScript 原稿
- `tests/calibration-preview.pdf` / `.png`：与打印内容一致的预览，可用于对比完整性

校准页包含：可打印区边框、距左/上边各 1 英寸的标线、中心十字、10 mm 刻度、字符
样本和实心块。

## 状态回读

后端在打印前、打印中和打印后调用 `LPGETSTATUS`，把 1284 状态字节翻译成 CUPS
`STATE:` 消息：

| 位 | 名称 | 含义 | 驱动动作 |
| --- | --- | --- | --- |
| `0x20` | `LP_POUTPA` | 缺纸 | `+media-empty`，并等待装纸（默认 120 秒） |
| `0x10` | `LP_PSELECD` | 选中/在线 | 未选中时 `+offline` |
| `0x08` | `LP_PERRORP` | 无错误（低有效，usblp 已反转） | 出错时报告设备错误 |
| `0x80` | `LP_PBUSY` | 忙 | 用于判断任务是否排空 |

健康空闲状态为 `0x18`（selected + no error，不忙）。缺纸等待时间默认 120 秒，
由 backend 启动时从 CUPS 服务环境读取 `FP620K_PAPER_WAIT`（秒，0 表示不等待）；
普通 `lp` 命令里的同名环境变量不会传给 backend，需要修改 CUPS 服务环境后重启。

手动查看状态（需要 root，因为要打开设备）：

```bash
sudo /usr/lib/cups/backend/fp620kp --status
sudo /usr/lib/cups/backend/fp620kp --monitor    # 每秒采样，Ctrl-C 结束
```

**注意**：FP-620K+ 的 1284 状态字节没有独立的“机盖打开”或“色带耗尽”位，厂商
Windows 驱动也没有机盖检测选项，因此本驱动不做这两类猜测。缺纸时打印机自身会
亮灯并暂停，装入纸张后继续；backend 同时按 `LP_POUTPA` 报告 `media-empty`，并
在检测到缺纸时等待装纸。

## 已知限制

- 整页都有内容的校准页实测：180×180 单向 76 秒、360×180 单向 109 秒、
  360×360 两遍交错 212 秒；普通文档通常快得多。
- 180×180、360×180、360×360 均已实机验证；120×180、90×180 已在代码中实现，
  但未逐一实机验证。
- CUPS 2.4 会提示“打印机驱动已弃用”，这是 CUPS 3.x 的整体方向，不影响当前使用。
  未来可把同一套 ESC/P2K 逻辑移植到 PAPPL Printer Application。
- 打印机没有硬件中文字库；中文依赖主机端字体和 Ghostscript。
- 厂商 Windows 驱动本身没有机盖检测选项，本驱动也不做没有依据的机盖/色带映射。

## 故障排查

```bash
lpstat -p FP620K -l                 # 队列和当前状态
ls -l /dev/usb/lp0                  # 设备是否存在
ls -l /usr/lib/cups/backend/fp620kp # 必须是 -rwx------ root root
sudo /usr/lib/cups/backend/fp620kp --status
```

- `cannot open /dev/usb/lp0`：后端权限不对，或 `usblp` 未绑定。重新执行
  `./install.sh` 并确认 backend 权限为 `0700`。
- 任务一直“正在打印”：针式打印机本身很慢；用 `ps aux | grep fp620kp` 确认后端
  仍在写数据。
- 打印位置偏移：先用 `fp620k-origin-x/y` 作业选项微调，确认后再改源码默认值。
- 竖线左右交错、文字有重影：确认作业使用 `PrintDirection=Unidirectional`；若仍无效，
  检查打印机本体的 `Prt_Direction` 是否被厂商工具设为 `0=双向`，改为 `1=单向` 或
  `2=自动`。
- 360×360 文字偏粗：这是两遍交错点重叠造成的正常现象；需要清晰文字时用默认的
  360×180。
