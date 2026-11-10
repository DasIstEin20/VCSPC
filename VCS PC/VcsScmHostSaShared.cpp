#include "StdAfx.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "ModelInfo.h"
#include "Pools.h"
#include "Object.h"
#include "World.h"
#include "VcsScmHostSaRadar.h"

namespace {
using Arg = VcsScmSaArg;

struct WeaponTypePair {
    int source;
    int native;
};

// VCS ReVcsWeaponProfiles.inc source IDs. SA supplies weapon behavior for
// these roles. Python uses SA's heavy pistol and Stubby its short shotgun.
// Unique VCS weapons without an SA counterpart remain explicit failures.
const WeaponTypePair weaponTypes[] = {
    {0, 0}, {1, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {10, 8}, {11, 9},
    {12, 16}, {13, 39}, {16, 17}, {17, 18}, {19, 22}, {20, 24},
    {21, 25}, {22, 27}, {23, 26}, {24, 32}, {25, 28}, {27, 29},
    {28, 31}, {29, 30}, {30, 34}, {32, 35}, {33, 37}, {35, 38},
    {36, 40}, {38, 43}
};

struct NativeCleanupEntry {
    signed char type;
    unsigned char padding[3];
    int handle;
};
struct NativeCleanup {
    NativeCleanupEntry entries[75];
    signed char count;
    unsigned char padding[3];
};
static_assert(sizeof(NativeCleanup) == 0x25c, "SA mission cleanup ABI");

typedef CPool<CObject, FakeClass<0x19c, CObject>> NativeObjectPool;

NativeObjectPool* ObjectPool()
{
    NativeObjectPool* pool;
    memcpy(&pool, reinterpret_cast<const void*>(0xB7449C), sizeof(pool));
    return pool;
}
}

CPed* VcsScmSaPed(int handle)
{
    CPedPool* pool = CPools::GetPedPool();
    return pool && handle >= 0 && !(handle & 0x80) && pool->IsValid(handle >> 8) ? pool->GetAt(handle) : nullptr;
}

CVehicle* VcsScmSaVehicle(int handle)
{
    CVehiclePool* pool = CPools::GetVehiclePool();
    return pool && handle >= 0 && !(handle & 0x80) && pool->IsValid(handle >> 8) ? pool->GetAt(handle) : nullptr;
}

CObject* VcsScmSaObject(int handle)
{
    // The stock GetObject function at 0x550050 omits its pool bounds check.
    // Its native object stride is 0x19c, beyond VCSPC's declared base view.
    NativeObjectPool* pool = ObjectPool();
    return pool && handle >= 0 && !(handle & 0x80) && pool->IsValid(handle >> 8) ? pool->GetAt(handle) : nullptr;
}

int VcsScmSaObjectPoolSize()
{
    NativeObjectPool* pool = ObjectPool();
    return pool ? pool->GetSize() : 0;
}

CObject* VcsScmSaObjectAtIndex(int index, int* handle)
{
    NativeObjectPool* pool = ObjectPool();
    CObject* object = pool && handle ? pool->GetAtIndex(index) : nullptr;
    if (object)
        *handle = pool->GetHandle(object);
    return object;
}

bool VcsScmSaPedExists(int handle) { return VcsScmSaPed(handle) != nullptr; }
bool VcsScmSaVehicleExists(int handle) { return VcsScmSaVehicle(handle) != nullptr; }
bool VcsScmSaObjectExists(int handle) { return VcsScmSaObject(handle) != nullptr; }

bool VcsScmSaModelLoaded(int model)
{
    bool loaded = false;
    return model >= 0 && model < 20000 &&
        CModelInfo::GetModelInfo(static_cast<uint16>(model)) &&
        VcsScmSaCall(0x0248, {Arg::Int(model)}, &loaded) && loaded;
}

bool VcsScmSaModelKind(int model, unsigned char kind)
{
    return VcsScmSaModelLoaded(model) &&
        CModelInfo::GetModelInfo(static_cast<uint16>(model))->GetModelType() == kind;
}

bool VcsScmSaVector(const float* xyz)
{
    return xyz && xyz[0] >= -20000000.0f && xyz[0] <= 20000000.0f &&
        xyz[1] >= -20000000.0f && xyz[1] <= 20000000.0f &&
        xyz[2] >= -20000000.0f && xyz[2] <= 20000000.0f;
}

bool VcsScmSaGroundPosition(const float* xyz, bool ground, float* position)
{
    if (!position || !VcsScmSaVector(xyz))
        return false;
    position[0] = xyz[0];
    position[1] = xyz[1];
    position[2] = xyz[2];
    return !ground || VcsScmSaCall(0x02CE, {
        Arg::Float(xyz[0]), Arg::Float(xyz[1]), Arg::Float(1000.0f), Arg::Out(position + 2)});
}

bool VcsScmSaMissionEntity(int handle, unsigned char type, bool mission)
{
    if (type < 1 || type > 3 || handle < 0 || (handle & 0x80))
        return false;
    // gta-reversed MissionCleanup.cpp and TheScripts.h, SA 1.0 US.
    // The native remove method also wakes entities waiting for collision.
    NativeCleanup* cleanup = reinterpret_cast<NativeCleanup*>(0xA90850);
    if (!mission) {
        reinterpret_cast<void (__thiscall*)(NativeCleanup*, int, unsigned char)>(0x4654B0)(cleanup, handle, type);
        return true;
    }
    for (const NativeCleanupEntry& entry : cleanup->entries) {
        if (entry.type == type && entry.handle == handle)
            return true;
    }
    if (cleanup->count < 0 || cleanup->count >= 75)
        return false;
    reinterpret_cast<void (__thiscall*)(NativeCleanup*, int, unsigned char)>(0x4637E0)(cleanup, handle, type);
    for (const NativeCleanupEntry& entry : cleanup->entries) {
        if (entry.type == type && entry.handle == handle)
            return true;
    }
    return false;
}

bool VcsScmSaDrawLocateMarker(unsigned int id, const float* center, const float* radius, bool three, float actorZ)
{
    if (!VcsScmSaVector(center) || !VcsScmSaVector(radius))
        return false;
    float position[] = {center[0], center[1], center[2]};
    if (!three) {
        bool found = false;
        const float ground = static_cast<float>(CWorld::FindGroundZFor3DCoord(
            center[0], center[1], actorZ + 2.0f, &found, nullptr));
        position[2] = found ? ground : actorZ;
    }
    return VcsScmSaLocateMarker(id, position, radius, three);
}

int VcsScmSaPedType(int source)
{
    static const int types[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        18, 19, 20, -1, 22, 23, -1
    };
    return source >= 0 && source < static_cast<int>(sizeof(types) / sizeof(types[0])) ? types[source] : -1;
}

int VcsScmSaWeaponType(int source)
{
    for (const WeaponTypePair& pair : weaponTypes) {
        if (pair.source == source)
            return pair.native;
    }
    return -1;
}

int VcsScmSaSourceWeapon(int nativeType)
{
    for (const WeaponTypePair& pair : weaponTypes) {
        if (pair.native == nativeType)
            return pair.source;
    }
    return -1;
}

int VcsScmSaDamageType(int source)
{
    switch (source) {
    case 18: return 19; // Rocket projectile.
    case 41: return 48; // Armour pseudo-weapon.
    case 42: return 49; // Rammed by a car.
    case 43: return 50; // Run over.
    case 44: return 51; // Explosion.
    case 45: return 52; // Drive-by Uzi.
    case 46: return 53; // Drowning.
    case 47: return 54; // Fall.
    case 48: return 55; // Unidentified damage.
    case 49: return 56; // Any melee weapon.
    case 50: return 57; // Any weapon.
    default: return VcsScmSaWeaponType(source);
    }
}

int VcsScmSaWeaponSlot(int sourceSlot)
{
    // Both GET_CHAR_WEAPON_IN_SLOT commands use one-based slots.
    // VCS's final slot combines SA camera and detonator slots and is handled
    // explicitly by the inventory adapter.
    static const int slots[] = {1, 2, 9, 3, 4, 5, 6, 8, 7};
    return sourceSlot >= 1 && sourceSlot <= 9 ? slots[sourceSlot - 1] : -1;
}
