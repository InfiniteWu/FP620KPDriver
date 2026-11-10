# FP620KPDriver

Jolimark FP-620K+ 24 针针式打印机的 Linux CUPS 驱动。

直接生成 ESC/P2K 图形命令，支持 USB 状态回读，不依赖 foomatic 或
Ghostscript 的 `epson` 输出设备。

## 特性

- 180×180、360×180、360×360 dpi
- 默认 360×180 单向打印，竖线笔直
- 缺纸、离线状态回读
- Debian/Ubuntu `.deb` 和源码安装

## 安装

从 [Releases](https://github.com/InfiniteWu/FP620KPDriver/releases/latest)
下载 `.deb`：

```bash
sudo apt install ./printer-driver-fp620kp_*.deb
sudo fp620kp-setup --default
```

可选：设为默认打印机并打印校准页。

```bash
sudo fp620kp-setup --default --test
```

源码安装：

```bash
git clone git@github.com:InfiniteWu/FP620KPDriver.git
cd FP620KPDriver/linux
./install.sh
```

默认队列为 A4、360×180 dpi、单向打印。

详细说明、校准和故障排查见 [linux/README.md](linux/README.md)。

## 许可证

MIT，见 [LICENSE](LICENSE)。
