# Lrix

<p align="center">
<a ><b>English</b></a> | <a href="README.zh-CN.md"><b>中文</b></a>
</p>

## 📌 Introduction
This is a scratch implemention of an Operating System based on [RISC-V](https://riscv.org).
For details of Lrix, you can visit [Report](./assets/LrixReport.pdf) or 
[报告](./assets/LrixReport-ZhCN.pdf).

> [!Caution]
> When you are viewing pdf files, and if you encounter issues like: 
> `Unable to render code block`, you can go [here](https://github.com/orgs/community/discussions/64419)
> to get some helps, but maybe it not works. And it is strange, sometimes on my
> old-fashioned computer, it renders normally, but just a time. However it works
> all well on my phone.

## 👀 Presentation
https://github.com/user-attachments/assets/a500f8f4-6f2b-42ed-9ab6-f23aaa7f8497

## 🚀 Quick start
> [!NOTE]
> Actually slow start.
> <br>
> If you have **riscv toolchain** and **qemu**(higher version than 5 maybe), you can go to [run](#run)
> <br>
> If you not have them, you need to obey the followings

To run this project, you need to have belowings:
### Dependencies
- **RISC-V toolchain**
    for compiling C programs.
    #### Installation steps
    
    > [!Warning]
    > git clone takes around 6.65 GB of disk and download size

    ##### Prerequisites
    - On **Ubuntu/Debain**
    ```bash
    sudo apt-get install autoconf automake autotools-dev curl python3 python3-pip python3-tomli libmpc-dev libmpfr-dev libgmp-dev gawk build-essential bison flex texinfo gperf libtool patchutils bc zlib1g-dev libexpat-dev ninja-build git cmake libglib2.0-dev libslirp-dev
    ```
    - On **Fedora/CentOS/RHEL OS**
    ```bash
    sudo yum install autoconf automake python3 libmpc-devel mpfr-devel gmp-devel gawk  bison flex texinfo patchutils gcc gcc-c++ zlib-devel expat-devel libslirp-devel
    ```
    - On **Arch Linux**
    ```bash
    sudo pacman -Syu curl python3 libmpc mpfr gmp base-devel texinfo gperf patchutils bc zlib expat libslirp
    ```
    ##### Installation
    ```bash
    git clone https://github.com/riscv/riscv-gnu-toolchain.git
    ./configure --prefix=/opt/riscv
    make -j$(nproc)
    echo 'export PATH="/opt/riscv/bin:$PATH"' >> ~/.bashrc
    source ~/.bashrc
    ```
    Then you can execute command:
    ```bash
    riscv64-unknown-elf-gcc --version
    ```
    to verify whether the toolchain is installed.
- **qemu-system-riscv**
    for create a virtual machine of RISC-V.
    #### Installation steps
    ##### Install from prebuild package
    - On **Ubuntu/Debain**
    ```bash
    sudo apt install qemu qemu-kvm libvirt-daemon-system libvirt-clients bridge-utils 
    sudo systemctl enable --now libvirtd
    ```
    - On **Fedora/CentOS/RHEL OS**
    ```bash
    sudo yum install -y qemu-kvm libvirt libvirt-python libguestfs-tools virt-install virt-manager
    # or
    sudo dnf install -y qemu-kvm libvirt libvirt-client virt-install virt-manager virt-top libguestfs-tools virt-viewer
    # then
    sudo systemctl enable --now libvirtd
    ```
    - On **Arch Linux**
    ```bash
    sudo pacman -S qemu-full qemu-img libvirt virt-install virt-manager virt-viewer dk2-ovmf dnsmasq swtpm guestfs-tools libosinfo tuned
    sudo systemctl enable --now libvirtd
    ```
    ##### Install from souce
    
    > [!Tip]
    > If you install qemu from source, may you need a higher version of **glibc**. When I compile it, the version is **2.35** at least. Considering that **glibc** is a very import system dependency, I recommand to compile **qemu** in docker instead. You can see the fowlling **docker(optional)** part.

    ```bash
    git clone https://github.com/qemu/qemu.git
    mkdir build
    cd build
    ../configure
    make -j$(nproc)
    sudo make install
    ```
    Then you can test it
    ```bash
    qemu-system-riscv64 --version
    ```
- **docker(optional)** for a higher version of **glibc** to compile qemu.
    > Only when you compile the **latest qemu** from source, you may need to install docker for a higher version of **glibc**.

    > [!Important]
    > Ensure your OS has a higher version of **glibc** then continue reading the rest of the content

    Compiling steps
    ```bash
    docker pull `os`:latest
    docker run -d --name osdev `os`:latest bash
    # if you compiled riscv-toolchain on your host os
    # you may need to add more prameters to start the 
    # docker container.
    # you can start it like this:
    # docker run -d -name osdev `os`:latest -v /opt/riscv:/opt/riscv bash
    # then you enter the container with higher glibc
    # and you can compile latest qemu
    git clone https://github.com/qemu/qemu.git
    mkdir build
    cd build
    ../configure
    make -j$(nproc)
    sudo make install
    ```
    Then you can test it
    ```bash
    qemu-system-riscv64 --version
    ```

### About
Flags of [Makefile](./Makefile):
### 1. Lrix Top-Level Build Parameters
| Variable Name | Description                                                                 |
|---------------|-----------------------------------------------------------------------------|
| KDIR          | Kernel directory, fixed value: `kernel`                                     |
| UDIR          | User program directory, fixed value: `usr`                                  |
| KIMG          | Kernel binary file path, fixed value: `kernel/build/kernel.bin`             |
| FS_DEBUG      | File system debug log toggle:<br>0 = Disable; 1 = Enable (default 0)        |
| VIRTIO        | VirtIO mode selection:<br>1 = legacy; 2 = modern (default 1)                |
| TRAP_DEBUG    | Trap debug:<br>0=disbale; 1=enable (default 0)                              |

### 2. Common Build Commands
| Command       | Description                                                               |
|---------------|---------------------------------------------------------------------------|
| make          | Equivalent to `make kernel`, builds kernel + user programs                |
| make run      | Builds the OS image and starts it via QEMU                                |
| make clean    | Cleans up output files generated during kernel build                      |
| make info     | Displays this help info + kernel sub-Makefile details                     |

### 3. Command Examples
| Example Command                       | Description                                                     |
|---------------------------------------|-----------------------------------------------------------------|
| make FS_DEBUG=1 VIRTIO=1 run          | Enable file system debug logs, use legacy VirtIO mode           |
| make FS_DEBUG=0 VIRTIO=2 run          | Disable file system debug logs, use modern VirtIO mode          |
| make TRAP_DEBUG=1 run                 | Enable trap debug logs                                          |

### Run
> [!Warning]
> To use flag `VIRTIO=2`, your qemu version needs to be higher than 5. <br>
> I had not tested actually the lowest supported qemu version, if you run success
> on a lower version of qemu, let me know. Thanks!
> When developing, I using **qemu** version **9.2**.
```bash
git clone https://github.com/lrisguan/Lrix.git
cd Lrix
# execute `make info` to see the flags and help.
# to run the os:
make run # VIRTIO=1, FS_DEBUG=0, TRAP_DEBUG=0
# or you can just run the script to start the os
./run.sh
```
> [!Tip]
> `make run` will automatically generate a disk.img for you. 
> `make clean` only deletes object files except disk.img. 
> So if you want to have a better control of whether to delete or create disk.img, 
> you can use [run.sh](./run.sh) to control it in a interactive environment.

## *Reference*
When implementing this project, I refer to *[xv6-riscv](https://github.com/mit-pdos/xv6-riscv)*

## License
[GPLv2.0](./License)
