#include "StdAfx.h"
#include "VcsScmHost.h"
#include "VcsScmSaCommand.h"
#include "VcsScmHostSaShared.h"
#include "ReVcsScriptVm.h"
#include "World.h"

namespace {
typedef VcsScmSaArg Arg;

bool Ground(float x, float y, float* z)
{
    return z && VcsScmSaCall(0x02CE, {
        Arg::Float(x), Arg::Float(y), Arg::Float(1000.0f), Arg::Out(z) });
}

bool PickupCreate(int model, unsigned char sourceType, int quantity,
                  float x, float y, float z, int* handle)
{
    if (!handle || model < 0 || quantity < 0) return false;
    // VCS slow respawn is 11. SA 11 is an armed nautical mine.
    int type;
    switch (sourceType) {
    case 2: type = 2; break;
    case 3: type = 3; break;
    case 8: type = 8; break;
    case 11: type = 15; break;
    default: return false;
    }
    return VcsScmSaCall(0x032B, { Arg::Int(model), Arg::Int(type),
        Arg::Int(quantity), Arg::Float(x), Arg::Float(y), Arg::Float(z), Arg::Out(handle) });
}

void PickupRemove(int handle)
{
    VcsScmSaCall(0x0215, { Arg::Int(handle) });
}

bool PickupCollected(int handle)
{
    bool collected = false;
    VcsScmSaCall(0x0214, { Arg::Int(handle) }, &collected);
    return collected;
}

bool CarGeneratorCreate(const int* p, int* handle)
{
    if (!p || !handle) return false;
    const int model = p[4] == -1 ? -1 : VcsScmModelBySource(p[4]);
    if (p[4] != -1 && model < 0) return false;
    return VcsScmSaCall(0x014B, {
        Arg::RawFloat(p[0]), Arg::RawFloat(p[1]), Arg::RawFloat(p[2]), Arg::RawFloat(p[3]),
        Arg::Int(model), Arg::Int(p[5]), Arg::Int(p[6]), Arg::Int(p[7]),
        Arg::Int(p[8]), Arg::Int(p[9]), Arg::Int(p[10]), Arg::Int(p[11]), Arg::Out(handle) });
}

bool CarGeneratorSwitch(unsigned short handle, int state)
{
    // SA CTheCarGenerators owns 500 slots and its native opcode omits a bounds check.
    return handle < 500 && VcsScmSaCall(0x014C, { Arg::Int(handle), Arg::Int(state) });
}

bool RoadSwitch(bool vehicles, bool enabled, const float* p)
{
    if (!p) return false;
    if (vehicles && enabled) return VcsScmSaCall(0x01E7, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
    if (vehicles) return VcsScmSaCall(0x01E8, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
    if (enabled) return VcsScmSaCall(0x022A, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
    return VcsScmSaCall(0x022B, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
}

void ClearArea(float x, float y, float z, float radius, unsigned char flags, bool ground)
{
    if (ground && !Ground(x, y, &z)) return;
    VcsScmSaCall(0x0395, { Arg::Float(x), Arg::Float(y), Arg::Float(z),
        Arg::Float(radius), Arg::Int(flags) });
}

void ClearCarsArea(const float* p)
{
    if (!p) return;
    VcsScmSaCall(0x03BA, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
}

void ClearPedsArea(const float* p)
{
    if (!p) return;
    VcsScmSaCall(0x042B, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) });
}

bool AreaOccupied(const int* p)
{
    if (!p) return false;
    bool occupied = false;
    VcsScmSaCall(0x0339, { Arg::RawFloat(p[0]), Arg::RawFloat(p[1]), Arg::RawFloat(p[2]),
        Arg::RawFloat(p[3]), Arg::RawFloat(p[4]), Arg::RawFloat(p[5]),
        Arg::Int(p[6] == 1), Arg::Int(p[7]), Arg::Int(p[8]), Arg::Int(p[9]), Arg::Int(p[10]) }, &occupied);
    return occupied;
}

bool MissionEntityCube(const float* p, bool* result)
{
    return p && result && VcsScmSaCall(0x038A, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
        Arg::Float(p[3]), Arg::Float(p[4]), Arg::Float(p[5]) }, result);
}

bool PointOnScreen(const float* p, float radius, bool* result)
{
    return p && result && VcsScmSaCall(0x00C2, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Float(radius) }, result);
}

void DensityMultiplier(bool cars, int bits)
{
    if (cars) VcsScmSaCall(0x01EB, { Arg::RawFloat(bits) });
    else VcsScmSaCall(0x03DE, { Arg::RawFloat(bits) });
}

void EmergencyServices(bool enabled)
{
    VcsScmSaCall(0x06D0, { Arg::Int(enabled) });
}

void FreeResprays(bool enabled)
{
    VcsScmSaCall(0x0335, { Arg::Int(enabled) });
}

void AllTaxisNitro(bool enabled)
{
    VcsScmSaCall(0x0572, { Arg::Int(enabled) });
}

bool CarCheatsActive()
{
    bool active = false;
    VcsScmSaCall(0x0445, {}, &active);
    return active;
}

void RandomCarPark(const int* p, bool mission)
{
    if (!p) return;
    VcsScmSaCall(0x03C5, { Arg::RawFloat(p[0]), Arg::RawFloat(p[1]),
        Arg::RawFloat(p[2]), Arg::RawFloat(p[3]) }, NULL, mission);
}

int MetresToFeet(int metres)
{
    int feet = 0;
    VcsScmSaCall(0x042D, { Arg::Int(metres), Arg::Out(&feet) });
    return feet;
}

bool ClosestPathNode(unsigned short source, const float* p, int rank, float* result)
{
    if (!p || !result) return false;
    switch (source) {
    case 0x01B4:
        return VcsScmSaCall(0x02C0, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
            Arg::Out(result), Arg::Out(result + 1), Arg::Out(result + 2) });
    case 0x01B5:
        return VcsScmSaCall(0x02C1, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
            Arg::Out(result), Arg::Out(result + 1), Arg::Out(result + 2) });
    case 0x025B:
        return VcsScmSaCall(0x03D3, { Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]),
            Arg::Out(result), Arg::Out(result + 1), Arg::Out(result + 2), Arg::Out(result + 3) });
    case 0x02F5:
        return rank > 0 && VcsScmSaCall(0x04D3, {
            Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(rank),
            Arg::Out(result), Arg::Out(result + 1), Arg::Out(result + 2) });
    default: return false;
    }
}

bool ClosestStraightRoad(const float* p, float* result)
{
    return p && result && VcsScmSaCall(0x04B9, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Float(p[3]), Arg::Float(p[4]),
        Arg::Out(result), Arg::Out(result + 1), Arg::Out(result + 2), Arg::Out(result + 3),
        Arg::Out(result + 4), Arg::Out(result + 5), Arg::Out(result + 6) });
}

bool RestartAdd(bool police, const float* p)
{
    if (!p) return false;
    const unsigned short count = *reinterpret_cast<const unsigned short*>(police ? 0xA43268 : 0xA4326C);
    if (count >= 10) return false;
    float z = p[2];
    if (z <= -100.0f && !Ground(p[0], p[1], &z)) return false;
    // VCS restart points have no SA cities-unlocked operand. Zero makes the
    // point available immediately in the Vice City conversion.
    if (police) return VcsScmSaCall(0x016D, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(z), Arg::Float(p[3]), Arg::Int(0) });
    return VcsScmSaCall(0x016C, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(z), Arg::Float(p[3]), Arg::Int(0) });
}

bool RestartOverride(const float* p)
{
    if (!p) return false;
    float z = p[2];
    if ((z == -100.0f || z <= -250.0f) && !Ground(p[0], p[1], &z)) return false;
    return VcsScmSaCall(0x016E, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(z), Arg::Float(p[3]) });
}

bool DeadCharacterPickupCoordinates(int handle, float* xyz)
{
    return xyz && VcsScmSaPedExists(handle) && VcsScmSaCall(0x04A5, {
        Arg::Int(handle), Arg::Out(xyz), Arg::Out(xyz + 1), Arg::Out(xyz + 2) });
}

bool ClosestObjectVisibility(const float* p, int model, bool sourceModel, bool visible)
{
    if (!p) return false;
    if (sourceModel) model = VcsScmModelBySource(model);
    return model >= 0 && VcsScmSaCall(0x0363, {
        Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Float(p[3]),
        Arg::Int(model), Arg::Int(visible) });
}

bool LineOfSightHit(const float* p, const int* flags, bool* result)
{
    if (!flags || !result || !VcsScmSaVector(p) || !VcsScmSaVector(p + 3)) return false;
    CColPoint point;
    CEntity* entity = NULL;
    *result = CWorld::ProcessLineOfSight(CVector(p[0], p[1], p[2]), CVector(p[3], p[4], p[5]),
        point, entity, flags[1] != 0, flags[2] != 0, flags[3] != 0,
        flags[4] != 0, flags[5] != 0, flags[0] != 0, false, false);
    return true;
}

bool ScriptVectorMath(unsigned short opcode, const float* values, float* result)
{
    if (!values || !result) return false;
    switch (opcode) {
    case 0x0372:
        *result = values[0] * values[2] + values[1] * values[3];
        return true;
    case 0x0373:
        *result = values[0] * values[3] + values[1] * values[4] + values[2] * values[5];
        return true;
    case 0x03EE:
        // Preserve VCS atan2(dx, -dy) and its original float degree constant.
        *result = atan2f(values[2] - values[0], values[1] - values[3]) * 57.2957763671875f;
        return true;
    default: return false;
    }
}

} // namespace

void VcsScmBindSaWorld(ReVcsScriptHost& host)
{
    host.pickupCreate = PickupCreate;
    host.pickupRemove = PickupRemove;
    host.pickupCollected = PickupCollected;
    host.carGeneratorCreate = CarGeneratorCreate;
    host.carGeneratorSwitch = CarGeneratorSwitch;
    host.roadSwitch = RoadSwitch;
    host.clearArea = ClearArea;
    host.clearCarsArea = ClearCarsArea;
    host.clearPedsArea = ClearPedsArea;
    host.areaOccupied = AreaOccupied;
    host.missionEntityCube = MissionEntityCube;
    host.pointOnScreen = PointOnScreen;
    host.densityMultiplier = DensityMultiplier;
    host.emergencyServices = EmergencyServices;
    host.freeResprays = FreeResprays;
    host.allTaxisNitro = AllTaxisNitro;
    host.carCheatsActive = CarCheatsActive;
    host.randomCarPark = RandomCarPark;
    host.metresToFeetInt = MetresToFeet;
    host.closestPathNode = ClosestPathNode;
    host.closestStraightRoad = ClosestStraightRoad;
    host.restartAdd = RestartAdd;
    host.restartOverride = RestartOverride;
    host.deadCharacterPickupCoordinates = DeadCharacterPickupCoordinates;
    host.closestObjectVisibility = ClosestObjectVisibility;
    host.lineOfSightHit = LineOfSightHit;
    host.scriptVectorMath = ScriptVectorMath;
}
