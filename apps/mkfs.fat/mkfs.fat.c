#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Uefi.h>
#include <X64/ProcessorBind.h>

#include <stdlib.h>

#define FAT32_SIGNATURE 0xAA55
#define FAT32_EOC 0x0FFFFFFF

typedef struct {
    unsigned char jmp[3];
    unsigned char oem[8];
    unsigned short bytes_per_sector;
    unsigned char sectors_per_cluster;
    unsigned short reserved_sectors;
    unsigned char fat_count;
    unsigned short root_dir_entries;
    unsigned short total_sectors_16;
    unsigned char media_descriptor;
    unsigned short fat_size_16;
    unsigned short sectors_per_track;
    unsigned short head_count;
    unsigned int hidden_sectors;
    unsigned int total_sectors_32;
    unsigned int fat_size_32;
    unsigned short flags;
    unsigned short version;
    unsigned int root_cluster;
    unsigned short fs_info_sector;
    unsigned short backup_boot_sector;
    unsigned char reserved[12];
    unsigned char drive_number;
    unsigned char reserved2;
    unsigned char signature;
    unsigned int volume_id;
    unsigned char volume_label[11];
    unsigned char fs_type[8];
    unsigned char boot_code[420];
    unsigned short signature2;
} __attribute__((packed)) fat32_boot_sector;

EFI_STATUS WriteFAT32BootSector(EFI_BLOCK_IO *BlockIo, UINT32 FatSize32,
                                UINT32 TotalSectors32) {
    EFI_STATUS Status;
    fat32_boot_sector BootSector;
    EFI_LBA LBA = 0;

    SetMem(&BootSector, sizeof(BootSector), 0);

    BootSector.jmp[0] = 0xEB;
    BootSector.jmp[1] = 0x58;
    BootSector.jmp[2] = 0x90;
    CopyMem(BootSector.oem, "MSWIN4.1", 8);
    BootSector.bytes_per_sector = 512;
    BootSector.sectors_per_cluster = 8;
    BootSector.reserved_sectors = 32;
    BootSector.fat_count = 2;
    BootSector.media_descriptor = 0xF8;
    BootSector.fat_size_32 = FatSize32;
    BootSector.root_cluster = 2;
    BootSector.fs_info_sector = 1;
    BootSector.backup_boot_sector = 6;
    BootSector.signature2 = FAT32_SIGNATURE;
    BootSector.total_sectors_32 = TotalSectors32;

    Status = BlockIo->WriteBlocks(BlockIo, BlockIo->Media->MediaId, LBA,
                                  sizeof(BootSector), &BootSector);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to write boot sector.\n");
        return Status;
    }

    return EFI_SUCCESS;
}

EFI_STATUS WriteFATTable(EFI_BLOCK_IO *BlockIo, UINT32 FatSize32,
                         UINT32 TotalSectors32) {
    EFI_STATUS Status;
    UINT32 FatStart = 32 * 512;
    UINT32 FatEntries = TotalSectors32 / 128;
    UINT8 *Fat =
        AllocateZeroPool(((FatEntries * 4) / BlockIo->Media->BlockSize + 1) *
                         BlockIo->Media->BlockSize);

    if (Fat == NULL) {
        Print(L"Memory allocation failed for FAT table.\n");
        return EFI_OUT_OF_RESOURCES;
    }

    Fat[0] = 0x0F;
    Fat[1] = 0xFF;
    Fat[2] = 0xFF;
    Fat[3] = 0xFF;

    Fat[4] = 0x0F;
    Fat[5] = 0xFF;
    Fat[6] = 0xFF;
    Fat[7] = 0xFF;

    EFI_LBA LBA = FatStart / BlockIo->Media->BlockSize;

    Status = BlockIo->WriteBlocks(
        BlockIo, BlockIo->Media->MediaId, LBA,
        (((FatEntries * 4) / BlockIo->Media->BlockSize + 1) *
         BlockIo->Media->BlockSize),
        Fat);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to write FAT table.\n");
        FreePool(Fat);
        return Status;
    }

    LBA = (FatStart + FatSize32 * 512) / BlockIo->Media->BlockSize;
    Status = BlockIo->WriteBlocks(
        BlockIo, BlockIo->Media->MediaId, LBA,
        (((FatEntries * 4) / BlockIo->Media->BlockSize + 1) *
         BlockIo->Media->BlockSize),
        Fat);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to write second FAT table.\n");
        FreePool(Fat);
        return Status;
    }

    FreePool(Fat);
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI FormatPartition(EFI_BLOCK_IO *BlockIo) {
    EFI_STATUS Status;
    UINT32 FatSize32 = 8192;
    UINT32 TotalSectors32 = 1000000;

    Status = WriteFAT32BootSector(BlockIo, FatSize32, TotalSectors32);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Status = WriteFATTable(BlockIo, FatSize32, TotalSectors32);
    if (EFI_ERROR(Status)) {
        return Status;
    }

    Print(L"Partition has been formatted to FAT32.\n");

    return EFI_SUCCESS;
}

EFI_STATUS GetPartitionHandles(OUT EFI_HANDLE ***disks, OUT UINTN *n_handles) {
    EFI_STATUS status = EFI_SUCCESS;

    if (disks == NULL || n_handles == NULL) {
        Print(L"GetPartitionNames: invalid parameters\n");
        return EFI_INVALID_PARAMETER;
    }
    *n_handles = 0;

    UINTN handleCount;
    EFI_HANDLE *allHandles;
    status = gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL,
                                     &handleCount, &allHandles);
    if (EFI_ERROR(status))
        return status;

    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            EFI_BLOCK_IO_PROTOCOL *blockIo;

            status = gBS->HandleProtocol(allHandles[handleIdx],
                                         &gEfiBlockIoProtocolGuid,
                                         (VOID **)&blockIo);
            if (EFI_ERROR(status)) {
                goto next_path1;
            }

            if (!blockIo->Media->LogicalPartition) {

                goto next_path1;
            }

            if (!blockIo->Media->MediaPresent) {

                goto next_path1;
            }
            (*n_handles)++;
            break;
        next_path1:
            path = NextDevicePathNode(path);
        }
    }

    *disks = (EFI_HANDLE **)malloc(*n_handles * sizeof(EFI_HANDLE *));
    if (*disks == NULL) {
        Print(L"GetPartitionNames: no memory(\n");
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN curIdx = 0;
    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            EFI_BLOCK_IO_PROTOCOL *blockIo;

            status = gBS->HandleProtocol(allHandles[handleIdx],
                                         &gEfiBlockIoProtocolGuid,
                                         (VOID **)&blockIo);
            if (EFI_ERROR(status)) {
                goto next_path2;
            }

            if (!blockIo->Media->LogicalPartition) {

                goto next_path2;
            }

            if (!blockIo->Media->MediaPresent) {

                goto next_path2;
            }
            (*disks)[curIdx++] = allHandles[handleIdx];
            break;
        next_path2:
            path = NextDevicePathNode(path);
        }
    }
    return status;
}

int main(int argc, char **argv) {
    EFI_BLOCK_IO *BlockIo;
    EFI_HANDLE **disks = NULL;
    UINTN n_part = 0;
    EFI_STATUS status = GetPartitionHandles(&disks, &n_part);
    EFI_BLOCK_IO_PROTOCOL *block_io;

    if (EFI_ERROR(status)) {
        Print(L"Error getting disks\n");
        return status;
    }

    if (disks == NULL) {
        Print(L"Error - out of memory!");
        return status;
    }

    status = gBS->HandleProtocol(disks[0], &gEfiBlockIoProtocolGuid,
                                 (VOID **)&BlockIo);
    if (EFI_ERROR(status)) {
        Print(L"Failed to get BlockIo protocol.\n");
        return status;
    }

    status = FormatPartition(BlockIo);
    if (EFI_ERROR(status)) {
        return status;
    }

    return EFI_SUCCESS;
}
