#include "general.h"
#include "dummy.h"

#define DRIVER_PAGES 16384
#define DRIVER_SIZE (DRIVER_PAGES * 4096)

static EFI_PHYSICAL_ADDRESS DriverBuffer = 0;

static const EFI_GUID ProtocolGuid
    = { 0x7c94b2a8, 0x51d3, 0x4f89, {0xa2, 0x6e, 0x19, 0xd7, 0xc4, 0x3b, 0x8e, 0x5f} };

static const EFI_GUID VirtualGuid
    = { 0x13FA7698, 0xC831, 0x49C7, { 0x87, 0xEA, 0x8F, 0x43, 0xFC, 0xC2, 0x51, 0x96 }};

static const EFI_GUID ExitGuid
    = { 0x27ABF055, 0xB1B8, 0x4C26, { 0x80, 0x48, 0x74, 0x8F, 0x37, 0xBA, 0xA2, 0xDF }};

typedef struct _DummyProtocalData{
    UINTN blank;
} DummyProtocalData;

static EFI_SET_VARIABLE oSetVariable = NULL;

static EFI_EVENT NotifyEvent = NULL;
static EFI_EVENT ExitEvent = NULL;
static BOOLEAN Virtual = FALSE;
static BOOLEAN Runtime = FALSE;

#define VARIABLE_NAME L"kR9vP2xM"
#define COMMAND_MAGIC 0x5F3B8A1C

typedef struct _MemoryCommand 
{
    int magic;
    int operation;
    unsigned long long data[10];
    int size;
} MemoryCommand;

typedef unsigned long long ptr64;
typedef int (*PsLookupProcessByProcessId)(void* processId, void** process);
typedef void* (*PsGetProcessSectionBaseAddress)(void* process);
typedef uintptr_t (stdcall *ExAllocatePool)(int type, uintptr_t size);
typedef void (stdcall *ExFreePool)(uintptr_t address);
typedef void (stdcall *StandardFuncStd)();
typedef void (fastcall *StandardFuncFast)();
typedef unsigned long (stdcall *DriverEntry)(void* driver, void* registry);

EFI_STATUS
RunCommand(MemoryCommand* cmd) 
{
    if (cmd->magic != COMMAND_MAGIC) 
    {
        return EFI_ACCESS_DENIED;
    }

    if (cmd->operation == 0) 
    {
        CopyMem(cmd->data[0], cmd->data[1], cmd->size);    
        return EFI_SUCCESS;
    }

    if (cmd->operation == 1) 
    {
        if (cmd->data[2] <= DRIVER_SIZE)
        {
            *(uintptr_t*)cmd->data[3] = (uintptr_t)DriverBuffer;
        }      
    }

    if (cmd->operation == 2) 
    {
        PsLookupProcessByProcessId GetProcessByPid = (PsLookupProcessByProcessId)cmd->data[0];
        PsGetProcessSectionBaseAddress GetBaseAddress = (PsGetProcessSectionBaseAddress)cmd->data[1];
        void* pid = (void*)cmd->data[2];
        void* resultAddr = (void*)cmd->data[3];
        void* ProcessPtr = 0;

        if (GetProcessByPid == NULL || GetBaseAddress == NULL || resultAddr == NULL)
        {
            return EFI_INVALID_PARAMETER;
        }

        // Find process by ID
        if (GetProcessByPid(pid, &ProcessPtr) < 0 || ProcessPtr == 0) 
        {
            *(ptr64*)resultAddr = 0; // Process not found
            return EFI_SUCCESS;
        }

        // Find process Base Address
        *(ptr64*)resultAddr = (ptr64)GetBaseAddress(ProcessPtr); // Return Base Address
        return EFI_SUCCESS;
    }

    if (cmd->operation == 3) 
    {
        void* function = cmd->data[0];
        StandardFuncStd stand = (StandardFuncStd)function;
        stand();
    }

    if (cmd->operation == 4) 
    {
        void* function = cmd->data[0];
        StandardFuncFast stand = (StandardFuncFast)function;
        stand();
    }

    if (cmd->operation == 5) 
    {
        void* function = cmd->data[0];
        DriverEntry entry = (DriverEntry)function;
        int status = entry(0, 0);
        *(int*)cmd->data[1] = status;
    }

    return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
HookedSetVariable(
    IN CHAR16 *VariableName,
    IN EFI_GUID *VendorGuid,
    IN UINT32 Attributes,
    IN UINTN DataSize,
    IN VOID *Data
	  ) 
{
    if (Virtual && Runtime) 
    {       
        if (VariableName != NULL && VariableName[0] != CHAR_NULL && VendorGuid != NULL) 
        {                     
            if (StrnCmp(VariableName, VARIABLE_NAME, 
                (sizeof(VARIABLE_NAME) / sizeof(CHAR16)) - 1) == 0) 
            {              
                if (DataSize == 0 && Data == NULL)
                {
                    return EFI_SUCCESS;
                }

                if (DataSize == sizeof(MemoryCommand)) 
                {
                    return RunCommand((MemoryCommand*)Data);
                }
            }
        }
    }
    
    return oSetVariable(VariableName, VendorGuid, Attributes, DataSize, Data);
}

VOID
EFIAPI
SetVirtualAddressMapEvent(
    IN EFI_EVENT Event,
    IN VOID* Context
    )
{  
    RT->ConvertPointer(0, &oSetVariable);
    RT->ConvertPointer(0, (VOID**)&DriverBuffer);
    RT->ConvertPointer(0, &oGetTime);
    RT->ConvertPointer(0, &oSetTime);
    RT->ConvertPointer(0, &oGetWakeupTime);
    RT->ConvertPointer(0, &oSetWakeupTime);
    RT->ConvertPointer(0, &oSetVirtualAddressMap);
    RT->ConvertPointer(0, &oConvertPointer);
    RT->ConvertPointer(0, &oGetVariable);
    RT->ConvertPointer(0, &oGetNextVariableName);
    RT->ConvertPointer(0, &oGetNextHighMonotonicCount);
    RT->ConvertPointer(0, &oResetSystem);
    RT->ConvertPointer(0, &oUpdateCapsule);
    RT->ConvertPointer(0, &oQueryCapsuleCapabilities);
    RT->ConvertPointer(0, &oQueryVariableInfo);
    
    RtLibEnableVirtualMappings();

    NotifyEvent = NULL;
    Virtual = TRUE;
}

VOID
EFIAPI
ExitBootServicesEvent(
	IN EFI_EVENT Event,
	IN VOID* Context
	)
{
    BS->CloseEvent(ExitEvent);
	ExitEvent = NULL;
    BS = NULL;
    Runtime = TRUE;
}

VOID*
SetServicePointer(
    IN OUT EFI_TABLE_HEADER *ServiceTableHeader,
    IN OUT VOID **ServiceTableFunction,
    IN VOID *NewFunction
    )
{
    if (ServiceTableFunction == NULL || NewFunction == NULL || *ServiceTableFunction == NULL)
        return NULL;

    ASSERT(BS != NULL);
    ASSERT(BS->CalculateCrc32 != NULL);

    CONST EFI_TPL Tpl = BS->RaiseTPL(TPL_HIGH_LEVEL);

    VOID* OriginalFunction = *ServiceTableFunction;
    *ServiceTableFunction = NewFunction;

    ServiceTableHeader->CRC32 = 0;
    BS->CalculateCrc32((UINT8*)ServiceTableHeader, ServiceTableHeader->HeaderSize, &ServiceTableHeader->CRC32);

    BS->RestoreTPL(Tpl);

    return OriginalFunction;
}

static
EFI_STATUS
EFI_FUNCTION
efi_unload(IN EFI_HANDLE ImageHandle)
{
    return EFI_ACCESS_DENIED;
}

EFI_STATUS
efi_main(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) 
{
    InitializeLib(ImageHandle, SystemTable);

    EFI_STATUS status = BS->AllocatePages(
        AllocateAnyPages,
        EfiRuntimeServicesCode,
        DRIVER_PAGES,
        &DriverBuffer);

    if (EFI_ERROR(status)) 
    {
        return status;
    }

    EFI_LOADED_IMAGE *LoadedImage = NULL;
    status = BS->OpenProtocol(ImageHandle, &LoadedImageProtocol,
                                        (void**)&LoadedImage, ImageHandle,
                                        NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    
    if (EFI_ERROR(status)) 
    {
        return status;
    }

    DummyProtocalData dummy = { 0 };
    status = LibInstallProtocolInterfaces(
      &ImageHandle, &ProtocolGuid,
      &dummy, NULL);

    if (EFI_ERROR(status)) 
    {
        return status;
    }

    LoadedImage->Unload = (EFI_IMAGE_UNLOAD)efi_unload;

    status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL,
                                TPL_NOTIFY,
                                SetVirtualAddressMapEvent,
                                NULL,
                                &VirtualGuid,
                                &NotifyEvent);

    if (EFI_ERROR(status)) 
    {
        return status;
    }

    status = BS->CreateEventEx(EVT_NOTIFY_SIGNAL,
                                TPL_NOTIFY,
                                ExitBootServicesEvent,
                                NULL,
                                &ExitGuid,
                                &ExitEvent);

    if (EFI_ERROR(status)) 
    {
        return status;
    }

    oSetVariable = (EFI_SET_VARIABLE)SetServicePointer(&RT->Hdr, (VOID**)&RT->SetVariable, (VOID**)&HookedSetVariable);

    oGetTime = (EFI_GET_TIME)SetServicePointer(&RT->Hdr, (VOID**)&RT->GetTime, (VOID**)&HookedGetTime);
    oSetTime = (EFI_SET_TIME)SetServicePointer(&RT->Hdr, (VOID**)&RT->SetTime, (VOID**)&HookedSetTime);
    oGetWakeupTime = (EFI_SET_TIME)SetServicePointer(&RT->Hdr, (VOID**)&RT->GetWakeupTime, (VOID**)&HookedGetWakeupTime);
    oSetWakeupTime = (EFI_SET_WAKEUP_TIME)SetServicePointer(&RT->Hdr, (VOID**)&RT->SetWakeupTime, (VOID**)&HookedSetWakeupTime);
    oSetVirtualAddressMap = (EFI_SET_VIRTUAL_ADDRESS_MAP)SetServicePointer(&RT->Hdr, (VOID**)&RT->SetVirtualAddressMap, (VOID**)&HookedSetVirtualAddressMap);
    oConvertPointer = (EFI_CONVERT_POINTER)SetServicePointer(&RT->Hdr, (VOID**)&RT->ConvertPointer, (VOID**)&HookedConvertPointer);
    oGetVariable = (EFI_GET_VARIABLE)SetServicePointer(&RT->Hdr, (VOID**)&RT->GetVariable, (VOID**)&HookedGetVariable);
    oGetNextVariableName = (EFI_GET_NEXT_VARIABLE_NAME)SetServicePointer(&RT->Hdr, (VOID**)&RT->GetNextVariableName, (VOID**)&HookedGetNextVariableName);
    oGetNextHighMonotonicCount = (EFI_GET_NEXT_HIGH_MONO_COUNT)SetServicePointer(&RT->Hdr, (VOID**)&RT->GetNextHighMonotonicCount, (VOID**)&HookedGetNextHighMonotonicCount);
    oResetSystem = (EFI_RESET_SYSTEM)SetServicePointer(&RT->Hdr, (VOID**)&RT->ResetSystem, (VOID**)&HookedResetSystem);
    oUpdateCapsule = (EFI_UPDATE_CAPSULE)SetServicePointer(&RT->Hdr, (VOID**)&RT->UpdateCapsule, (VOID**)&HookedUpdateCapsule);
    oQueryCapsuleCapabilities = (EFI_QUERY_CAPSULE_CAPABILITIES)SetServicePointer(&RT->Hdr, (VOID**)&RT->QueryCapsuleCapabilities, (VOID**)&HookedQueryCapsuleCapabilities);
    oQueryVariableInfo = (EFI_QUERY_VARIABLE_INFO)SetServicePointer(&RT->Hdr, (VOID**)&RT->QueryVariableInfo, (VOID**)&HookedQueryVariableInfo);

    return EFI_SUCCESS;
}
