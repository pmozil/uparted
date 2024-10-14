#include <Uefi.h>
#include <X64/ProcessorBind.h>

#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <stdio.h>
#include <stdlib.h>

// EFI_STATUS GetPartitionNames(OUT CHAR16 ***partitionNames,
//                              OUT UINTN *partitionCount) {
//     EFI_STATUS status = EFI_SUCCESS;
//
//     if (partitionNames == NULL || partitionCount == NULL) {
//         puts("GetPartitionNames: invalid parameters");
//         return EFI_INVALID_PARAMETER;
//     }
//     *partitionCount = 0;
//
//     UINTN handleCount;
//     EFI_HANDLE *allHandles;
//     status =
//         gBS->LocateHandleBuffer(ByProtocol,
//         &gEfiSimpleFileSystemProtocolGuid,
//                                 NULL, &handleCount, &allHandles);
//     if (EFI_ERROR(status))
//         return status;
//
//     for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
//         EFI_DEVICE_PATH_PROTOCOL *path =
//             DevicePathFromHandle(allHandles[handleIdx]);
//         while (path != NULL && !IsDevicePathEndType(path)) {
//             if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
//                 DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
//                 (*partitionCount)++;
//                 break;
//             }
//             path = NextDevicePathNode(path);
//         }
//     }
//
//     *partitionNames = malloc(*partitionCount * sizeof(CHAR16 *));
//     if (*partitionNames == NULL) {
//         puts("GetPartitionNames: no memory(");
//         return EFI_OUT_OF_RESOURCES;
//     }
//
//     UINTN curIdx;
//     for (UINTN handleIdx = 0; handleIdx < handleCount; handleIdx++) {
//         EFI_HANDLE deviceHandle = allHandles[handleIdx];
//         EFI_DEVICE_PATH_PROTOCOL *path = DevicePathFromHandle(deviceHandle);
//         while (path != NULL && !IsDevicePathEndType(path)) {
//             if (DevicePathType(path) == MEDIA_DEVICE_PATH &&
//                 DevicePathSubType(path) == MEDIA_HARDDRIVE_DP) {
//                 (*partitionNames)[curIdx++] =
//                     ConvertDevicePathToText(path, TRUE, TRUE);
//             }
//             path = NextDevicePathNode(path);
//         }
//     }
//     return status;
// }

EFI_STATUS PrintPartitionNames() {
    EFI_STATUS status = EFI_SUCCESS;

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
                Print(L"Device Path - %s\n",
                      ConvertDevicePathToText(path, TRUE, TRUE));
            }
            path = NextDevicePathNode(path);
        }
    }

    return status;
}

int main(IN int Argc, IN char **Argv) {
    PrintPartitionNames();

    return 0;
}
