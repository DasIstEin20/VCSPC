#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "Pools.h"
#include "World.h"

namespace {
using Arg = VcsScmSaArg;

CPed* Ped(int handle)
{
    CPedPool* pool = CPools::GetPedPool();
    return pool && handle >= 0 && !(handle & 0x80) && pool->IsValid(handle >> 8) ? pool->GetAt(handle) : nullptr;
}

bool InControl(int handle)
{
    const CPed* ped = Ped(handle);
    // The native SA CPed::IsPedInControl contract, Ped.cpp at 0x5E3960.
    return ped && !ped->pedFlags.bIsLanding && !ped->pedFlags.bIsInTheAir &&
        ped->pedState != 54 && ped->pedState != 55 && ped->pedState != 63;
}

bool Fighting(int handle)
{
    const CPed* ped = Ped(handle);
    return ped && ped->pedState == 17;
}

bool WaitOnFoot(int handle)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x05BA, {Arg::Int(handle), Arg::Int(-1)});
}

bool ClearObjective(int handle)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x0687, {Arg::Int(handle)});
}

bool StopLooking(int handle)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x0647, {Arg::Int(handle)});
}

bool LookAtCharacter(int handle, int target)
{
    return VcsScmSaPedExists(handle) && VcsScmSaPedExists(target) &&
        VcsScmSaCall(0x05BF, {Arg::Int(handle), Arg::Int(target), Arg::Int(60000)});
}

bool PlayerFootHeight(float* z)
{
    const int focus = CWorld::PlayerInFocus;
    if (focus < 0 || focus >= NUM_PLAYERS || !CWorld::Players[focus].pPed)
        return false;
    volatile float base = CWorld::Players[focus].pPed->GetCoords().z - 1.04f;
    *z = base + 0.1f;
    return *z >= -20000000.0f && *z <= 20000000.0f;
}

bool GotoOnFoot(int handle, float x, float y, unsigned char pace)
{
    float position[] = {x, y, 0.0f};
    if (pace > 2 || !VcsScmSaPedExists(handle) || !PlayerFootHeight(position + 2) || !VcsScmSaVector(position))
        return false;
    static const int moveStates[] = {4, 6, 7}; // WALK, RUN, SPRINT in SA eMoveState.
    return VcsScmSaCall(0x05D3, {Arg::Int(handle), Arg::Float(x), Arg::Float(y),
        Arg::Float(position[2]), Arg::Int(moveStates[pace]), Arg::Int(-1)});
}

bool FollowPath(int handle, const float* xyz, float radius, int move)
{
    float position[3];
    return xyz && radius >= 0.0f && radius <= 10000.0f && VcsScmSaPedExists(handle) &&
        VcsScmSaGroundPosition(xyz, xyz[2] == -100.0f || xyz[2] <= -250.0f, position) &&
        VcsScmSaCall(0x0A2E, {Arg::Int(handle), Arg::Float(position[0]), Arg::Float(position[1]),
            Arg::Float(position[2]), Arg::Int(move == 1 ? 6 : 4), Arg::Int(999999), Arg::Float(radius)});
}

bool Wander(int handle, int direction)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    // A fixed source direction seeds the native heading. Subsequent route
    // choices belong to SA's ordinary wander task and pedestrian paths.
    if (direction >= 0 && direction <= 7 &&
        !VcsScmSaCall(0x0173, {Arg::Int(handle), Arg::Float(direction * 45.0f)}))
        return false;
    return VcsScmSaCall(0x05DE, {Arg::Int(handle)});
}

bool FacePoint(int handle, const float* point)
{
    CPed* ped = Ped(handle);
    if (!ped || !VcsScmSaVector(point))
        return false;
    if (ped->pedFlags.bInVehicle)
        return true;
    const CVector& position = ped->GetCoords();
    const float degrees = atan2f(position.x - point[0], point[1] - position.y) * 57.29577951308232f;
    return VcsScmSaCall(0x0173, {Arg::Int(handle), Arg::Float(degrees)});
}

bool FaceCoordinates(const int* parameters)
{
    if (!parameters)
        return false;
    float position[3];
    memcpy(position, parameters + 1, sizeof(position));
    return FacePoint(parameters[0], position);
}

bool FaceCharacter(int handle, int target)
{
    CPed* ped = Ped(target);
    if (!ped)
        return false;
    CEntity* entity = ped->pedFlags.bInVehicle && ped->GetVehiclePtr() ?
        static_cast<CEntity*>(ped->GetVehiclePtr()) : static_cast<CEntity*>(ped);
    const CVector& point = entity->GetCoords();
    const float position[] = {point.x, point.y, point.z};
    return FacePoint(handle, position);
}

bool Objective(unsigned short sourceOpcode, int handle, int target)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    if (sourceOpcode == 0x00F1)
        return VcsScmSaCall(0x0633, {Arg::Int(handle)});
    if (!VcsScmSaVehicleExists(target))
        return false;
    switch (sourceOpcode) {
    case 0x00E8: return VcsScmSaCall(0x05CD, {Arg::Int(handle), Arg::Int(target)});
    case 0x00E9: return VcsScmSaCall(0x05CA, {Arg::Int(handle), Arg::Int(target), Arg::Int(-1), Arg::Int(-1)});
    case 0x00EA: return VcsScmSaCall(0x05CB, {Arg::Int(handle), Arg::Int(target), Arg::Int(-1)});
    case 0x00EC: return VcsScmSaCall(0x0672, {Arg::Int(handle), Arg::Int(target)});
    default: return false;
    }
}

bool VehicleAction(unsigned short sourceOpcode, int handle, int)
{
    CPed* ped = Ped(handle);
    CVehiclePool* vehicles = CPools::GetVehiclePool();
    if (!ped || !vehicles || sourceOpcode != 0x030E || !ped->GetVehiclePtr())
        return false;
    const int vehicle = vehicles->GetHandle(ped->GetVehiclePtr());
    return VcsScmSaVehicleExists(vehicle) &&
        VcsScmSaCall(0x0676, {Arg::Int(handle), Arg::Int(vehicle)});
}

bool Posture(unsigned short sourceOpcode, int handle, int enabled, int duration)
{
    if (sourceOpcode != 0x0309 || !VcsScmSaPedExists(handle))
        return false;
    if (enabled)
        return VcsScmSaCall(0x05C5, {Arg::Int(handle), Arg::Int(duration)});
    return VcsScmSaCall(0x04EB, {Arg::Int(handle), Arg::Int(0)});
}

bool WaitState(int handle, int sourceState, int duration, bool)
{
    CPed* ped = Ped(handle);
    if (!ped || ped->pedState == 17)
        return true;
    // These source wait states have dedicated SA task owners. COWER's SA
    // command has no duration field, so only its default duration is admitted.
    // SA owns looping of these task animations. Source repeat never overrides
    // a supplied timer, which must remain attached to this request.
    switch (sourceState) {
    case 14: return VcsScmSaCall(0x05C5, {Arg::Int(handle), Arg::Int(duration)});
    case 15:
    case 18:
        return duration < 0 && VcsScmSaCall(0x05C3, {Arg::Int(handle)});
    case 17:
    case 33:
        return VcsScmSaCall(0x05C4, {Arg::Int(handle), Arg::Int(duration)});
    default: return false;
    }
}
}

void VcsScmBindSaTasks(ReVcsScriptHost& host)
{
    host.characterInControl = InControl;
    host.characterFighting = Fighting;
    host.characterWaitOnFoot = WaitOnFoot;
    host.characterIdle = WaitOnFoot;
    host.characterClearObjective = ClearObjective;
    host.characterStopLooking = StopLooking;
    host.characterLookAtCharacter = LookAtCharacter;
    host.characterGotoOnFoot = GotoOnFoot;
    host.characterFollowPath = FollowPath;
    host.characterWander = Wander;
    host.characterFaceCoordinates = FaceCoordinates;
    host.characterFaceCharacter = FaceCharacter;
    host.characterObjective = Objective;
    host.characterVehicleAction = VehicleAction;
    host.characterPosture = Posture;
    host.characterWaitState = WaitState;
}
