#include <Uefi.h>
#include <X64/ProcessorBind.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <stdio.h>
#include <stdlib.h>

EFI_STATUS GetPartitionNames(OUT EFI_DEVICE_PATH_PROTOCOL ***partitions,
                             OUT UINTN *partitionCount) {
    EFI_STATUS status = EFI_SUCCESS;

    if (partitions == NULL || partitionCount == NULL) {
        puts("GetPartitionNames: invalid parameters");
        return EFI_INVALID_PARAMETER;
    }
    *partitionCount = 0;

    UINTN handleCount;
    EFI_HANDLE *allHandles;
    status =
        gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                NULL, &handleCount, &allHandles);
    if (EFI_ERROR(status))
        return status;

    for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
        EFI_DEVICE_PATH_PROTOCOL *path =
            DevicePathFromHandle(allHandles[handleIdx]);
        while (path != NULL && !IsDevicePathEndType(path)) {
            if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
                DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
                (*partitionCount)++;
            }
            path = NextDevicePathNode(path);
        }
    }

    *partitions = (EFI_DEVICE_PATH_PROTOCOL **)malloc(
        *partitionCount * sizeof(EFI_DEVICE_PATH_PROTOCOL *));
    if (*partitions == NULL) {
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
                (*partitions)[curIdx++] = path;
            }
            path = NextDevicePathNode(path);
        }
    }
    return status;
}

int main(IN int Argc, IN char **Argv) {
    EFI_DEVICE_PATH_PROTOCOL **partitions = NULL;
    UINTN n_part = 0;
    EFI_STATUS status = GetPartitionNames(&partitions, &n_part);

    if (EFI_ERROR(status)) {
        puts("Error getting partitions\n");
        return status;
    }

    if (partitions == NULL) {
        puts("Error - out of memory!");
        return status;
    }

    for (UINTN i = 0; i < n_part; i++) {
        Print(L"Device Path %d - %s\n", i,
              ConvertDevicePathToText(partitions[i], FALSE, TRUE));
    }

    free(partitions);

    return 0;
}
