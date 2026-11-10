#include "StdAfx.h"
#include "VcsScmHostSaGarages.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "ReVcsScriptVm.h"
#include "Pools.h"

namespace {
typedef VcsScmSaArg Arg;

// Reviewed SA 1.0 layout, gta-sa-reversed Garage.h. Fixed-width fields keep
// the mirrored layout independent of the compiler's native pointer size.
struct NativeGarage {
    float position[3];
    float directionA[2];
    float directionB[2];
    float topZ;
    float width;
    float height;
    float left;
    float right;
    float front;
    float back;
    float doorPosition;
    unsigned int timeToOpen;
    unsigned int targetCar;
    char name[8];
    unsigned char type;
    unsigned char doorState;
    unsigned char flags;
    unsigned char originalType;
    unsigned char audio[0x88];
};
static_assert(sizeof(NativeGarage) == 0xD8, "SA garage layout changed");
static_assert(offsetof(NativeGarage, name) == 0x44, "SA garage name offset changed");

VcsScmGarageState garageState;
bool hooksInstalled = false;

NativeGarage* Garage(int index)
{
    const int count = *reinterpret_cast<const int*>(0x96C024);
    return index >= 0 && index < count && index < 50 ?
        reinterpret_cast<NativeGarage*>(0x96C048) + index : NULL;
}

int GarageIndex(const NativeGarage* garage)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(garage);
    if (address < 0x96C048 || address >= 0x96C048 + 50 * 0xD8) return -1;
    const uintptr_t offset = address - 0x96C048;
    return offset % 0xD8 == 0 ? static_cast<int>(offset / 0xD8) : -1;
}

int __cdecl Capacity(const NativeGarage* garage)
{
    const int index = GarageIndex(garage);
    if (index >= 0 && garageState.capacity[index] != 255) return garageState.capacity[index];
    return garage->type == 16 ? 2 : 4;
}

void __fastcall StoreCars(NativeGarage* garage, void*, void* storedCars, int maximum)
{
    const int index = GarageIndex(garage);
    if (index >= 0 && garageState.capacity[index] != 255)
        maximum = Min(maximum, static_cast<int>(garageState.capacity[index]));
    reinterpret_cast<void (__thiscall*)(NativeGarage*, void*, int)>(0x449900)(garage, storedCars, maximum);
}

void __fastcall UpdateGarage(NativeGarage* garage, void*, int index)
{
    const int actual = GarageIndex(garage);
    const bool locked = actual >= 0 && garageState.locked[actual] != 0;
    if (locked) {
        if (garage->doorState == 0 || garage->doorState == 5) return;
        if (garage->doorState == 1 || garage->doorState == 3) garage->doorState = 2;
    }
    reinterpret_cast<void (__thiscall*)(NativeGarage*, int)>(0x44AA50)(garage, index);
    if (locked && (garage->doorState == 1 || garage->doorState == 3)) garage->doorState = 2;
}

#if defined(_MSC_VER) && defined(_M_IX86)
const unsigned int capacityReturn = 0x44BF27;
// Replaces the inlined SA capacity selection. Preserve EAX/EDX and the exact
// pending CountCarsInHideoutGarage(type) stack argument at the continuation.
__declspec(naked) void GarageCapacityThunk()
{
    __asm {
        push eax
        push eax
        push edx
        push esi
        call Capacity
        add esp, 4
        mov edi, eax
        pop edx
        pop eax
        mov ecx, edi
        jmp dword ptr [capacityReturn]
    }
}
#endif

int GarageType(unsigned char source)
{
    if (source <= 5) return source;
    if (source >= 14 && source <= 16) return source + 2;
    if (source == 17) return 19;
    if (source >= 22 && source <= 30) return source + 2;
    return -1;
}

bool GarageCreate(const int* raw, int* handle)
{
    if (!raw || !handle || !hooksInstalled) return false;
    float p[8];
    memcpy(p, raw, sizeof(p));
    for (int i = 0; i < 8; ++i) if (!(p[i] > -10000.0f && p[i] < 10000.0f)) return false;
    const int type = GarageType(static_cast<unsigned char>(raw[8]));
    const float ax = p[3] - p[0], ay = p[4] - p[1];
    const float bx = p[5] - p[0], by = p[6] - p[1];
    if (type <= 0 || p[2] >= p[7] || fabsf(ax * by - ay * bx) < 0.01f) return false;
    const float aLength = sqrtf(ax * ax + ay * ay), bLength = sqrtf(bx * bx + by * by);

    // Reuse the matching IPL garage when the conversion already supplies it.
    // This preserves its door/audio owner and avoids consuming a second slot.
    int count = *reinterpret_cast<const int*>(0x96C024);
    if (count < 0 || count > 50) return false;
    int selected = -1;
    for (int i = 0; i < count; ++i) {
        const NativeGarage* garage = Garage(i);
        if (fabsf(garage->position[0] - p[0]) < 0.01f &&
            fabsf(garage->position[1] - p[1]) < 0.01f &&
            fabsf(garage->position[2] - p[2]) < 0.01f &&
            fabsf(garage->topZ - p[7]) < 0.01f &&
            fabsf(garage->width - aLength) < 0.01f && fabsf(garage->height - bLength) < 0.01f &&
            fabsf(garage->directionA[0] - ax / aLength) < 0.001f &&
            fabsf(garage->directionA[1] - ay / aLength) < 0.001f &&
            fabsf(garage->directionB[0] - bx / bLength) < 0.001f &&
            fabsf(garage->directionB[1] - by / bLength) < 0.001f) {
            selected = i;
            break;
        }
    }
    *handle = -1;
    if (selected < 0 && count == 50) return true;
    const int index = selected >= 0 ? selected : count;
    char name[8];
    sprintf_s(name, sizeof(name), "VCS%04d", index);
    if (selected >= 0) {
        memcpy(Garage(index)->name, name, sizeof(name));
        reinterpret_cast<void (__cdecl*)(short, unsigned char, unsigned int)>(0x4476D0)(
            static_cast<short>(index), static_cast<unsigned char>(type), 0);
    } else {
        reinterpret_cast<void (__cdecl*)(float, float, float, float, float, float, float, float,
            unsigned char, unsigned int, char*, unsigned int)>(0x4471E0)(
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            static_cast<unsigned char>(type), 0, name, 0);
        if (*reinterpret_cast<const int*>(0x96C024) != count + 1) return false;
    }
    garageState.capacity[index] = 4;
    garageState.locked[index] = 0;
    *handle = index;
    return true;
}

bool GarageMaxCars(int index, unsigned char maximum)
{
    if (!Garage(index) || !hooksInstalled || maximum > 4) return false;
    garageState.capacity[index] = maximum;
    return true;
}

bool GarageRotateDoor(short index)
{
    NativeGarage* garage = Garage(index);
    if (!garage) return false;
    garage->flags |= 8; // Native m_bDoorOpensUp, consumed by the real door matrix builder.
    return true;
}

bool GarageChangeType(short index, unsigned char source)
{
    const int type = GarageType(source);
    NativeGarage* garage = Garage(index);
    if (!garage || type < 0) return false;
    char name[9] = {};
    memcpy(name, garage->name, 8);
    return VcsScmSaCall(0x02FA, { Arg::Text(name), Arg::Int(type) });
}

bool GarageDoor(int index, bool open)
{
    NativeGarage* garage = Garage(index);
    if (!garage) return false;
    char name[9] = {};
    memcpy(name, garage->name, 8);
    if (open) return VcsScmSaCall(0x0360, { Arg::Text(name) });
    return VcsScmSaCall(0x0361, { Arg::Text(name) });
}

bool GarageLock(short index, int value)
{
    if (!Garage(index) || !hooksInstalled) return false;
    garageState.locked[index] = value != 0;
    if (value) return GarageDoor(index, false);
    return true;
}

bool GarageState(unsigned short source, short index, bool* result)
{
    NativeGarage* garage = Garage(index);
    if (!garage || !result) return false;
    char name[9] = {};
    memcpy(name, garage->name, 8);
    if (source == 0x0242) return VcsScmSaCall(0x03B0, { Arg::Text(name) }, result);
    if (source == 0x0243) return VcsScmSaCall(0x03B1, { Arg::Text(name) }, result);
    return false;
}

bool GarageContainsVehicle(short index, int handle, bool* result)
{
    if (!Garage(index) || !result || !VcsScmSaVehicleExists(handle)) return false;
    *result = reinterpret_cast<bool (__cdecl*)(short, CVehicle*)>(0x449740)(
        index, CPools::GetVehiclePool()->GetAt(handle));
    return true;
}

bool GarageTargetVehicle(short index, int handle)
{
    NativeGarage* garage = Garage(index);
    if (!garage) return false;
    CVehicle* vehicle = VcsScmSaVehicleExists(handle) ? CPools::GetVehiclePool()->GetAt(handle) : NULL;
    // The native owner manages its entity reference and accepts a null target.
    reinterpret_cast<void (__cdecl*)(short, CVehicle*)>(0x447C40)(index, vehicle);
    return true;
}

bool GarageDroppedVehicle(short index, bool* result)
{
    const NativeGarage* garage = Garage(index);
    if (!garage || !result) return false;
    *result = garage->doorState == 5;
    return true;
}

bool GarageResprayed(short index)
{
    return Garage(index) && reinterpret_cast<bool (__cdecl*)(short)>(0x447E10)(index);
}

bool GarageFreeBombs(bool enabled)
{
    *reinterpret_cast<unsigned char*>(0x96C00A) = enabled;
    return true;
}

} // namespace

bool VcsScmInstallSaGarageHooks()
{
    if (hooksInstalled) return true;
#if defined(_MSC_VER) && defined(_M_IX86)
    const unsigned char capacityBytes[] = {0x33,0xC9,0x3C,0x10,0x0F,0x95,0xC1,0x50,0x8D,0x4C,0x09,0x02,0x8B,0xF9};
    const unsigned char saveCall[] = {0xE8,0x11,0xF7,0xFF,0xFF};
    const unsigned char storeCall[] = {0xE8,0xB7,0xDA,0xFF,0xFF};
    const unsigned char updateCall[] = {0xE8,0x3B,0xE1,0xFF,0xFF};
    // These exact sites were verified in the supplied gta-vcs.exe, SHA-256
    // deb7ba9c5cc47805b5e1344f6ea32fefa02ba6183304fcef10b15898b0ba1fb1.
    if (memcmp(reinterpret_cast<void*>(0x44BF19), capacityBytes, sizeof(capacityBytes)) ||
        memcmp(reinterpret_cast<void*>(0x44A1EA), saveCall, sizeof(saveCall)) ||
        memcmp(reinterpret_cast<void*>(0x44BE44), storeCall, sizeof(storeCall)) ||
        memcmp(reinterpret_cast<void*>(0x44C910), updateCall, sizeof(updateCall))) return false;
    VcsScmResetSaGarages();
    Memory::InjectHook(0x44BF19, &GarageCapacityThunk, PATCH_JUMP);
    Memory::InjectHook(0x44A1EA, &StoreCars, PATCH_CALL);
    Memory::InjectHook(0x44BE44, &StoreCars, PATCH_CALL);
    Memory::InjectHook(0x44C910, &UpdateGarage, PATCH_CALL);
    hooksInstalled = true;
    return true;
#else
    return false;
#endif
}

void VcsScmResetSaGarages()
{
    memset(garageState.capacity, 255, sizeof(garageState.capacity));
    memset(garageState.locked, 0, sizeof(garageState.locked));
}

void VcsScmCaptureSaGarages(VcsScmGarageState& state) { state = garageState; }

bool VcsScmValidateSaGarages(const VcsScmGarageState& state)
{
    for (int i = 0; i < 50; ++i)
        if ((state.capacity[i] > 4 && state.capacity[i] != 255) || state.locked[i] > 1) return false;
    return true;
}

bool VcsScmRestoreSaGarages(const VcsScmGarageState& state)
{
    if (!VcsScmValidateSaGarages(state)) return false;
    garageState = state;
    return true;
}

void VcsScmBindSaGarages(ReVcsScriptHost& host)
{
    host.garageCreate = GarageCreate;
    host.garageMaxCars = GarageMaxCars;
    host.garageRotateDoor = GarageRotateDoor;
    host.garageChangeType = GarageChangeType;
    host.garageDoor = GarageDoor;
    host.garageLock = GarageLock;
    host.garageState = GarageState;
    host.garageContainsVehicle = GarageContainsVehicle;
    host.garageTargetVehicle = GarageTargetVehicle;
    host.garageDroppedVehicle = GarageDroppedVehicle;
    host.garageResprayed = GarageResprayed;
    host.garageFreeBombs = GarageFreeBombs;
}
