#include <Uefi.h>
#include <X64/ProcessorBind.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include <stdio.h>
#include <stdlib.h>

EFI_STATUS GetPartitionHandles(OUT EFI_HANDLE ***disks, OUT UINTN *n_handles) {
    EFI_STATUS status = EFI_SUCCESS;

    if (disks == NULL || n_handles == NULL) {
        puts("GetPartitionNames: invalid parameters");
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
            if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
                DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
                (*n_handles)++;
                break;
            }
            path = NextDevicePathNode(path);
        }
    }

    *disks = (EFI_HANDLE **)malloc(*n_handles * sizeof(EFI_HANDLE *));
    if (*disks == NULL) {
        puts("GetPartitionNames: no memory(");
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN curIdx = 0;
    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
                DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
                (*disks)[curIdx++] = allHandles[handleIdx];
                break;
            }
            path = NextDevicePathNode(path);
        }
    }
    return status;
}

int main(IN int Argc, IN char **Argv) {
    EFI_HANDLE **disks = NULL;
    UINTN n_part = 0;
    EFI_STATUS status = GetPartitionHandles(&disks, &n_part);
    EFI_BLOCK_IO_PROTOCOL *block_io;

    if (EFI_ERROR(status)) {
        puts("Error getting disks\n");
        return status;
    }

    if (disks == NULL) {
        puts("Error - out of memory!");
        return status;
    }

    for (UINTN i = 0; i < n_part; i++) {
        EFI_DEVICE_PATH_PROTOCOL *path = DevicePathFromHandle(disks[i]);
        Print(L"Device Path %d - %s\n", i,
              ConvertDevicePathToText(path, FALSE, TRUE));

        status = gBS->HandleProtocol(disks[i], &gEfiBlockIoProtocolGuid,
                                     (VOID **)&block_io);
        if (EFI_ERROR(status)) {
            puts("Failed to open handle to disk");
            continue;
        }

        if (block_io->Media->MediaPresent) {
            UINT64 disk_size =
                (block_io->Media->LastBlock + 1) * block_io->Media->BlockSize;
            Print(L"Disk Size: %llu bytes\n", disk_size);
            // test read on disks not 0
            // if (i == 0) {
            //     continue;
            // }
            // EFI_BLOCK_IO_PROTOCOL *block_io;
            // EFI_HANDLE *handle = disks[i];
            // EFI_STATUS status =
            //     gBS->HandleProtocol(handle, &gEfiBlockIoProtocolGuid,
            //             (VOID **)&block_io);
            // if (EFI_ERROR(status)) {
            //     puts("Failed to open handle to device");
            //     return 1;
            // }
            // char *buffer = malloc(disk_size);
            // if (buffer == NULL) {
            //     puts("Failed to allocate buffer");
            //     return 1;
            // }
            // 108 - block number, where to start = 108 * 512 byte number. 512 - blocksize
            // block_io->Media->BlockSize - how many to read. in this case 1 block
            // status = block_io->ReadBlocks(block_io, block_io->Media->MediaId,
            //         108, block_io->Media->BlockSize, buffer);
            // if (EFI_ERROR(status) || buffer == NULL) {
            //     puts("Failed to read from device");
            //     continue;
            // }
            // for (int k = 0; k < block_io->Media->BlockSize; k++) {
            //     if (buffer[k] != '\0') {
            //         Print(L"%d: %c\n", k, buffer[k]);
            //     }
            // }
            free(buffer);
        }
    }

    free(disks);

    return 0;
}
