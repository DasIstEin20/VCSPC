#include "StdAfx.h"
#include "VcsScmSaCommand.h"
#include "VcsScmHostSaShared.h"
#include "ReVcsScriptVm.h"
#include "Pools.h"

namespace {
typedef VcsScmSaArg Arg;

// The source VM narrows script-fire operands to int16 array indices. SA uses
// a generation in the high word. Read the current native owner when adapting
// that index, including after a world restore. No side table can go stale.
struct NativeFire {
    unsigned char flags;
    unsigned char padding;
    unsigned short generation;
    float position[3];
    unsigned int entity;
    unsigned int creator;
    unsigned int timeToBurn;
    float strength;
    unsigned char generations;
    unsigned char removalDistance;
    unsigned short padding2;
    unsigned int effect;
};
static_assert(sizeof(NativeFire) == 0x28, "SA fire layout changed");

NativeFire* Fire(int index)
{
    return index >= 0 && index < 60 ? reinterpret_cast<NativeFire*>(0xB71F80) + index : NULL;
}

int FireReference(short index)
{
    const NativeFire* fire = Fire(index);
    return fire ? static_cast<int>((unsigned(fire->generation) << 16) | unsigned(index)) : -1;
}

bool EntityHasFire(const void* entity)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(entity);
    for (int i = 0; i < 60; ++i) {
        const NativeFire* fire = Fire(i);
        if ((fire->flags & 1) && fire->entity == address) return true;
    }
    return false;
}

void ConvertFireOutput(int* handle)
{
    if (*handle != -1) *handle &= 0xFFFF;
}

bool VehicleStartFire(int handle, int* fire)
{
    if (!fire || !VcsScmSaVehicleExists(handle)) return false;
    // VCS preserves an already attached fire. SA START_CAR_FIRE replaces it.
    *fire = -1;
    if (EntityHasFire(CPools::GetVehiclePool()->GetAt(handle))) return true;
    if (!VcsScmSaCall(0x0325, { Arg::Int(handle), Arg::Out(fire) })) return false;
    ConvertFireOutput(fire);
    return true;
}

bool CharacterStartFire(int handle, int* fire)
{
    if (!fire || !VcsScmSaPedExists(handle)) return false;
    *fire = -1;
    if (EntityHasFire(CPools::GetPedPool()->GetAt(handle))) return true;
    if (!VcsScmSaCall(0x0326, { Arg::Int(handle), Arg::Out(fire) })) return false;
    ConvertFireOutput(fire);
    return true;
}

bool WorldStartFire(const float* p, int* fire)
{
    if (!p || !fire) return false;
    float z = p[2];
    if ((z == -100.0f || z <= -250.0f) && !VcsScmSaCall(0x02CE, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(1000.0f), Arg::Out(&z) })) return false;
    // SA requires explicit propagation/size operands for its fire owner.
    if (!VcsScmSaCall(0x02CF, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(z),
        Arg::Int(1), Arg::Int(1), Arg::Out(fire) })) return false;
    ConvertFireOutput(fire);
    return true;
}

bool ScriptFireExtinguished(short index, bool* result)
{
    const NativeFire* fire = Fire(index);
    if (!fire || !result) return false;
    if (!(fire->flags & 2)) {
        *result = !(fire->flags & 1);
        return true;
    }
    return VcsScmSaCall(0x02D0, { Arg::Int(FireReference(index)) }, result);
}

bool ScriptFireRemove(short index)
{
    const NativeFire* fire = Fire(index);
    if (!fire) return false;
    return !(fire->flags & 2) || VcsScmSaCall(0x02D1, { Arg::Int(FireReference(index)) });
}

void ScriptFiresRemoveAll()
{
    VcsScmSaCall(0x031A, {});
}

int ExplosionType(int source)
{
    // SA inserted weak rockets and merged the two VCS helicopter variants.
    // Keep the distinct VCS heli2/heli-bomb variants unbound by this switch.
    switch (source) {
    case 0: return 0;
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 4: return 5;
    case 5: return 6;
    case 6: return 7;
    case 8: return 8;
    case 9: return 9;
    case 10: return 10;
    default: return -1;
    }
}

bool ExplosionAdd(const float* p, int source)
{
    const int type = ExplosionType(source);
    if (!p || type < 0) return false;
    for (int i = 0; i < 3; ++i)
        if (!(p[i] >= -10000 && p[i] <= 10000)) return false;
    return VcsScmSaCall(0x020C, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(type) });
}

bool ExplosionInArea(int source, const float* p, bool* result)
{
    if (!p || !result) return false;
    for (int i = 0; i < 6; ++i)
        if (!(p[i] >= -10000 && p[i] <= 10000)) return false;
    if (source < 0 || source > 11) {
        *result = false;
        return true;
    }
    const int type = ExplosionType(source);
    return type >= 0 && VcsScmSaCall(0x0356, {
        Arg::Int(type), Arg::Float(Min(p[0], p[3])), Arg::Float(Min(p[1], p[4])), Arg::Float(Min(p[2], p[5])),
        Arg::Float(Max(p[0], p[3])), Arg::Float(Max(p[1], p[4])), Arg::Float(Max(p[2], p[5])) }, result);
}

} // namespace

void VcsScmBindSaFires(ReVcsScriptHost& host)
{
    host.vehicleStartFire = VehicleStartFire;
    host.characterStartFire = CharacterStartFire;
    host.worldStartFire = WorldStartFire;
    host.scriptFireExtinguished = ScriptFireExtinguished;
    host.scriptFireRemove = ScriptFireRemove;
    host.scriptFiresRemoveAll = ScriptFiresRemoveAll;
    host.explosionAdd = ExplosionAdd;
    host.explosionInArea = ExplosionInArea;
}
