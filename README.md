# Uparted - a port of GNU parted to UEFI

This repo is a port of uparted to UEFI, including the scripts to build it.

## Requirements

libuuid libuuid-devel nasm acpica-tools

## Building

To start, you need to set up the repository.
Do this:

```sh
git clone https://github.com/pmozil/uparted
cd uparted
. scripts/setup_repo.sh
```

This inits and updates the submodules.


To esetup env, do


```sh
. setup_env.sh
```

 The space is important - the vars need to be exported to this shell!


To compile, oyu can do


```sh
./scripts/build_apps.sh <PATH_TO_DSC_FILE>
```

Or

```sh
./scripts/build_app_pkg.sh
```


To launch, you need to install OVMF, and export the path to it, like this:

```sh
export OVMF_PATH=/usr/share/edk2-ovmf/x64/OVMF.fd
```

And then

```sh
./scripts/run_qemu.sh <PATH_TO_EFI_EXEC_OUTPUT_DIR>
```

Like this:

```sh
./scripts/run_qemu.sh ./Build/AppPkg/DEBUG_GCC/X64/
```

Where
```sh
[petro@shire uparted]$ ls ./Build/AppPkg/DEBUG_GCC/X64/
AppPkg               GetHostByDns.debug   Hello.efi                    RecvDgram.debug
ArithChk.debug       GetHostByDns.efi     Main.debug                   RecvDgram.efi
ArithChk.efi         GetHostByName.debug  Main.efi                     SetHostName.debug
DataSink.debug       GetHostByName.efi    MdeModulePkg                 SetHostName.efi
DataSink.efi         GetNameInfo.debug    MdePkg                       SetSockOpt.debug
DataSource.debug     GetNameInfo.efi      OobRx.debug                  SetSockOpt.efi
DataSource.efi       GetNetByAddr.debug   OobRx.efi                    ShellPkg
DemoApp.debug        GetNetByAddr.efi     OobTx.debug                  StdLib
DemoApp.efi          GetNetByName.debug   OobTx.efi                    TOOLS_DEF.X64
Enquire.debug        GetNetByName.efi     OrderedCollectionTest.debug  UefiCpuPkg
Enquire.efi          GetServByName.debug  OrderedCollectionTest.efi    WebServer.debug
GetAddrInfo.debug    GetServByName.efi    RawIp4Rx.debug               WebServer.efi
GetAddrInfo.efi      GetServByPort.debug  RawIp4Rx.efi
GetHostByAddr.debug  GetServByPort.efi    RawIp4Tx.debug
GetHostByAddr.efi    Hello.debug          RawIp4Tx.efi
```
