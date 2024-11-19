#include "uuid/uuid.h"
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/Rng.h>
#include <Uefi.h>
#include <stdint.h>
#include <string.h>

void uuid_generate(uuid_t out) {
    EFI_STATUS Status;
    EFI_RNG_PROTOCOL *RngProtocol;
    EFI_GUID GeneratedUuid;

    Status =
        gBS->LocateProtocol(&gEfiRngProtocolGuid, NULL, (VOID **)&RngProtocol);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to locate RNG protocol.\n");
    }

    // Generate random bytes for the UUID
    Status = RngProtocol->GetRNG(RngProtocol, NULL, sizeof(EFI_GUID),
                                 (UINT8 *)&GeneratedUuid);
    if (EFI_ERROR(Status)) {
        Print(L"Failed to generate random bytes.\n");
    }

    // Set the UUID version and variant fields
    GeneratedUuid.Data3 &= 0x0FFF;  // Clear version bits
    GeneratedUuid.Data3 |= 0x4000;  // Set version to 4 (random UUID)
    GeneratedUuid.Data4[0] &= 0x3F; // Clear variant bits
    GeneratedUuid.Data4[0] |= 0x80; // Set variant to RFC 4122

    memcpy(&out, &GeneratedUuid.Data2, 16);
}
