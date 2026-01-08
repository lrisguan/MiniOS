# Lrix

<p align="center">
<a href="README.md"><b>English</b></a> | <a ><b>中文</b></a>
</p>

## 📌 简介
这是一个基于 [RISC-V](https://riscv.org) 从零开始实现的操作系统。
关于 Lrix 的细节，你可以参见 [Report](./assets/LrixReport.pdf) or [报告](./assets/LrixReport-ZhCN.pdf).

> [!Caution]
> 当你查看 PDF 文件时，如果遇到类似以下问题：
> `无法渲染代码块`，你可以前往 [这里](https://github.com/orgs/community/discussions/64419)
> 获取一些帮助，但可能不一定有效。有点奇怪，有时在我那台老旧的电脑上，它能够正常渲染，但只是偶尔。但在我的手机上，一切都能正常工作。


## 👀 演示
https://github.com/user-attachments/assets/a500f8f4-6f2b-42ed-9ab6-f23aaa7f8497

## 🚀 快速开始
> [!NOTE]
> 实际上是慢速开始。
> <br>
> 如果你已经安装了 **riscv 工具链** 和 **qemu**（版本可能需高于5），可以直接跳转到[运行](#运行)
> <br>
> 如果没有，请遵循以下步骤

运行本项目需要以下环境：
### 依赖项
- **RISC-V 工具链**
    用于编译 C 程序。
    #### 安装步骤
    
    > [!WARNING]
    > git clone 大约需要 6.65 GB 的磁盘空间和下载量

    ##### 前置条件
    - **Ubuntu/Debian** 系统
    ```bash
    sudo apt-get install autoconf automake autotools-dev curl python3 python3-pip python3-tomli libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev ninja-build git cmake libglib2.0-dev libslirp-dev
    ```
    - **Fedora/CentOS/RHEL** 系统
    ```bash
    sudo yum install autoconf automake python3 libmpc-devel mpfr-devel gmp-devel gawk  bison flex texinfo patchutils gcc gcc-c++ zlib-devel expat-devel libslirp-devel
    ```
    - **Arch Linux** 系统
    ```bash
    sudo pacman -Syu curl python3 libmpc mpfr gmp base-devel texinfo gperf patchutils bc zlib expat libslirp
    ```
    ##### 安装步骤
    ```bash
    git clone https://github.com/riscv/riscv-gnu-toolchain.git 
    ./configure --prefix=/opt/riscv
    make -j$(nproc)
    echo 'export PATH="/opt/riscv/bin:$PATH"' >> ~/.bashrc
    source ~/.bashrc
    ```
    然后可以执行命令：
    ```bash
    riscv64-unknown-elf-gcc --version
    ```
    验证工具链是否安装成功。
- **qemu-system-riscv**
    用于创建 RISC-V 虚拟机。
    #### 安装步骤
    ##### 从预编译包安装
    - **Ubuntu/Debian** 系统
    ```bash
    sudo apt install qemu qemu-kvm libvirt-daemon-system libvirt-clients bridge-utils 
    sudo systemctl enable --now libvirtd
    ```
    - **Fedora/CentOS/RHEL** 系统
    ```bash
    sudo yum install -y qemu-kvm libvirt libvirt-python libguestfs-tools virt-install virt-manager
    # 或者
    sudo dnf install -y qemu-kvm libvirt libvirt-client virt-install virt-manager virt-top libguestfs-tools virt-viewer
    # 然后
    sudo systemctl enable --now libvirtd
    ```
    - **Arch Linux** 系统
    ```bash
    sudo pacman -S qemu-full qemu-img libvirt virt-install virt-manager virt-viewer edk2-ovmf dnsmasq swtpm guestfs-tools libosinfo tuned
    sudo systemctl enable --now libvirtd
    ```
    ##### 从源码安装
    
    > [!TIP]
    > 如果从源码安装 qemu，可能需要较高版本的 **glibc**。我编译时，最低版本为 **2.35**。考虑到 **glibc** 是非常重要的系统依赖，我建议在 docker 中编译 **qemu**。可参考下面的 **docker（可选）** 部分。

    ```bash
    git clone https://github.com/qemu/qemu.git 
    mkdir build
    cd build
    ../configure
    make -j$(nproc)
    sudo make install
    ```
    然后可以测试
    ```bash
    qemu-system-riscv64 --version
    ```
- **docker（可选）** 用于编译 qemu 所需的更高版本 **glibc**
    > 仅当你从源码编译 **最新版 qemu** 时，才可能需要安装 docker 以获取更高版本的 **glibc**

    > [!IMPORTANT]
    > 请确保你的操作系统具有较高版本的 **glibc**，然后再继续阅读以下内容

    编译步骤
    ```bash
    docker pull `os`:latest
    docker run -d --name osdev `os`:latest bash
    # 如果你在宿主机上编译了 riscv-toolchain
    # 可能需要添加更多参数来启动
    # docker 容器。
    # 可以这样启动：
    # docker run -d -name osdev `os`:latest -v /opt/riscv:/opt/riscv bash
    # 然后进入具有更高 glibc 版本的容器
    # 即可编译最新版 qemu
    git clone https://github.com/qemu/qemu.git 
    mkdir build
    cd build
    ../configure
    make -j$(nproc)
    sudo make install
    ```
    然后可以测试
    ```bash
    qemu-system-riscv64 --version
    ```

### 关于
[Makefile](./Makefile) 的编译标志：
### 1. Lrix 顶层构建参数
| 变量名         | 说明                                                                        |
|---------------|-----------------------------------------------------------------------------|
| KDIR          | 内核目录，固定值：`kernel`                                                    |
| UDIR          | 用户程序目录，固定值：`usr`                                                   |
| KIMG          | 内核二进制文件路径，固定值：`kernel/build/kernel.bin`                          |
| FS_DEBUG      | 文件系统调试日志开关：<br>0 = 禁用；1 = 启用（默认 0）                          |
| VIRTIO        | VirtIO 模式选择：<br>1 = 传统模式；2 = 现代模式（默认 1）                       |
| TRAP_DEBUG    | 陷阱调试：<br>0=禁用；1=启用（默认 0）                                         |

### 2. 常用构建命令
| 命令           | 说明                                                                      |
|---------------|---------------------------------------------------------------------------|
| make          | 等同于 `make kernel`，构建内核 + 用户程序                                   |
| make run      | 构建操作系统镜像并通过 QEMU 启动                                            |  
| make clean    | 清理内核构建过程中生成的输出文件                                             |
| make info     | 显示此帮助信息 + 内核子 Makefile 详情                                       |

### 3. 命令示例
| 示例命令                               | 说明                                              |
|---------------------------------------|---------------------------------------------------|
| make FS_DEBUG=1 VIRTIO=1 run          | 启用文件系统调试日志，使用传统 VirtIO 模式           |
| make FS_DEBUG=0 VIRTIO=2 run          | 禁用文件系统调试日志，使用现代 VirtIO 模式           |
| make TRAP_DEBUG=1 run                 | 启用陷入调试日志                                   |

### 运行
> [!WARNING]
> 使用 `VIRTIO=2` 标志需要 qemu 版本高于 5。<br>
> 我实际上没有测试过支持的最低 qemu 版本，如果你在更低版本的 qemu 上运行成功，请告诉我。谢谢！
> 开发时我使用的 **qemu** 版本为 **9.2**。

```bash
git clone https://github.com/lrisguan/Lrix.git 
cd Lrix
# 执行 `make info` 查看标志和帮助信息。
# 运行操作系统：
make run # VIRTIO=1, FS_DEBUG=0, TRAP_DEBUG=0
# 或者直接运行脚本来启动系统
./run.sh
```

> [!TIP]
> `make run` 会自动为你生成 disk.img。
> `make clean` 仅删除目标文件，不会删除 disk.img。
> 因此，如果你想更好地控制是否删除或创建 disk.img，
> 可以使用 [run.sh](./run.sh) 在交互式环境中进行控制。

## *参考*
实现本项目时，我参考了 *[xv6-riscv](https://github.com/mit-pdos/xv6-riscv)*

## 许可证
[GPLv2.0](./License)
