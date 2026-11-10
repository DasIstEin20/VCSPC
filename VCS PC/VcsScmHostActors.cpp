#include "StdAfx.h"
#include "VcsScmHostActors.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "ReVcsScriptVm.h"
#include "Pools.h"
#include "Object.h"

// Native pool and state contracts were checked against gta-sa-reversed
// afbc26d7, Pools.cpp and Scripts/Commands/CharacterCommands.cpp, for SA 1.0 US.
// The VM retains VCS handles, operands and condition reduction. These functions
// access the host's live entities and never manufacture a successful lookup.
namespace {

CPed* Ped(int handle)
{
    CPedPool* pool = CPools::GetPedPool();
    if (!pool || !pool->IsValid(handle >> 8))
        return NULL;
    return pool->GetAt(handle);
}

CVehicle* Vehicle(int handle)
{
    CVehiclePool* pool = CPools::GetVehiclePool();
    if (!pool || !pool->IsValid(handle >> 8))
        return NULL;
    return pool->GetAt(handle);
}

CObject* Object(int handle)
{
    return VcsScmSaObject(handle);
}

bool IsDead(CPed* ped)
{
    // SA ePedState DIE, DEAD and DIE_BY_STEALTH. Health alone does not describe
    // the engine's dying task transition or the script's dead predicate.
    return !ped || ped->pedState == 54 || ped->pedState == 55 || ped->pedState == 56;
}

CVehicle* OccupiedVehicle(CPed* ped)
{
    return ped && ped->pedFlags.bInVehicle ? ped->GetVehiclePtr() : NULL;
}

bool Position(CEntity* entity, float* xyz)
{
    if (!entity || !xyz)
        return false;
    const CVector& position = entity->GetCoords();
    xyz[0] = position.x;
    xyz[1] = position.y;
    xyz[2] = position.z;
    return true;
}

bool Heading(CEntity* entity, float* degrees)
{
    if (!entity || !degrees)
        return false;
    float angle = entity->GetHeading() * 57.29577951308232f;
    angle = fmodf(angle, 360.0f);
    if (angle < 0.0f)
        angle += 360.0f;
    *degrees = angle;
    return true;
}

bool Offset(CEntity* entity, const float* local, float* world)
{
    if (!entity || !local || !world)
        return false;
    const CVector result = entity->TransformFromObjectSpace(CVector(local[0], local[1], local[2]));
    world[0] = result.x;
    world[1] = result.y;
    world[2] = result.z;
    return true;
}

bool Proofs(CPhysical* entity, const int* flags)
{
    if (!entity || !flags)
        return false;
    entity->bBulletProof = flags[0] != 0;
    entity->bFireProof = flags[1] != 0;
    entity->bExplosionProof = flags[2] != 0;
    entity->bCollisionProof = flags[3] != 0;
    entity->bMeeleProof = flags[4] != 0;
    return true;
}

bool CharacterExists(int handle) { return Ped(handle) != NULL; }
bool VehicleExists(int handle) { return Vehicle(handle) != NULL; }
bool ObjectExists(int handle) { return Object(handle) != NULL; }
bool CharacterDead(int handle) { return IsDead(Ped(handle)); }
bool CharacterAlive(int handle) { return !IsDead(Ped(handle)); }
bool CharacterInVehicle(int handle) { return OccupiedVehicle(Ped(handle)) != NULL; }

bool CharacterOnFoot(int handle)
{
    CPed* ped = Ped(handle);
    return ped && !OccupiedVehicle(ped);
}

bool CharacterInSpecificVehicle(int handle, int vehicleHandle)
{
    CVehicle* occupied = OccupiedVehicle(Ped(handle));
    return occupied && occupied == Vehicle(vehicleHandle);
}

bool CharacterInModel(int handle, int sourceModel)
{
    CVehicle* occupied = OccupiedVehicle(Ped(handle));
    const int model = VcsScmModelBySource(sourceModel);
    return occupied && model >= 0 && occupied->GetModelIndex() == model;
}

bool CharacterInBoat(int handle)
{
    CVehicle* vehicle = OccupiedVehicle(Ped(handle));
    return vehicle && vehicle->GetSubClass() == VEHICLE_BOAT;
}

bool CharacterOnBike(int handle)
{
    CVehicle* vehicle = OccupiedVehicle(Ped(handle));
    return vehicle && (vehicle->GetSubClass() == VEHICLE_BIKE || vehicle->GetSubClass() == VEHICLE_BMX);
}

bool CharacterInFlyingVehicle(int handle)
{
    CVehicle* vehicle = OccupiedVehicle(Ped(handle));
    if (!vehicle)
        return false;
    const eVehicleType type = vehicle->GetSubClass();
    return type == VEHICLE_HELI || type == VEHICLE_PLANE || type == VEHICLE_FHELI || type == VEHICLE_FPLANE;
}

bool CharacterVehicleNoSave(int handle, int* vehicleHandle)
{
    CPed* ped = Ped(handle);
    if (!ped || !vehicleHandle)
        return false;
    CVehicle* vehicle = OccupiedVehicle(ped);
    *vehicleHandle = vehicle ? CPools::GetVehiclePool()->GetHandle(vehicle) : -1;
    return true;
}

bool CharacterRawCoordinates(int handle, float* xyz) { return Position(Ped(handle), xyz); }
bool VehicleCoordinates(int handle, float* xyz) { return Position(Vehicle(handle), xyz); }
bool ObjectCoordinates(int handle, float* xyz) { return Position(Object(handle), xyz); }

bool CharacterCoordinates(int handle, float* xyz)
{
    CPed* ped = Ped(handle);
    CVehicle* vehicle = OccupiedVehicle(ped);
    return Position(vehicle ? static_cast<CEntity*>(vehicle) : static_cast<CEntity*>(ped), xyz);
}

bool CharacterHeadingGet(int handle, float* degrees)
{
    CPed* ped = Ped(handle);
    CVehicle* vehicle = OccupiedVehicle(ped);
    return Heading(vehicle ? static_cast<CEntity*>(vehicle) : static_cast<CEntity*>(ped), degrees);
}

bool VehicleHeadingGet(int handle, float* degrees) { return Heading(Vehicle(handle), degrees); }
bool ObjectHeadingGet(int handle, float* degrees) { return Heading(Object(handle), degrees); }
bool CharacterOffset(int handle, const float* local, float* world) { return Offset(Ped(handle), local, world); }
bool VehicleOffset(int handle, const float* local, float* world) { return Offset(Vehicle(handle), local, world); }
bool ObjectOffset(int handle, const float* local, float* world) { return Offset(Object(handle), local, world); }
bool CharacterProofs(int handle, const int* flags) { return Proofs(Ped(handle), flags); }
bool VehicleProofs(int handle, const int* flags) { return Proofs(Vehicle(handle), flags); }

bool CharacterHealthGet(int handle, int* health)
{
    CPed* ped = Ped(handle);
    if (!ped || !health || !(ped->fHealth >= -2147483648.0f && ped->fHealth < 2147483648.0f))
        return false;
    *health = static_cast<int>(ped->fHealth);
    return true;
}

bool CharacterArmourGet(int handle, int* armour)
{
    CPed* ped = Ped(handle);
    if (!ped || !armour || !(ped->fArmour >= -2147483648.0f && ped->fArmour < 2147483648.0f))
        return false;
    *armour = static_cast<int>(ped->fArmour);
    return true;
}

bool CharacterHealthGreater(int handle, int threshold)
{
    CPed* ped = Ped(handle);
    return ped && ped->fHealth > threshold;
}

bool CharacterArmourGreater(int handle, int threshold)
{
    CPed* ped = Ped(handle);
    return ped && ped->fArmour > threshold;
}

bool CharacterInWater(int handle)
{
    CPed* ped = Ped(handle);
    return ped && ped->bSubmergedInWater;
}

bool VehicleInWater(int handle)
{
    CVehicle* vehicle = Vehicle(handle);
    return vehicle && vehicle->bSubmergedInWater;
}

bool VehicleDead(int handle)
{
    CVehicle* vehicle = Vehicle(handle);
    // VCS vehicle-dead also counts a drowning vehicle before wrecked status.
    return !vehicle || vehicle->nStatus == 5 || vehicle->GetVehicleFlags().bIsDrowning;
}

bool CharacterClearWeapons(int handle)
{
    CPed* ped = Ped(handle);
    if (!ped)
        return false;
    // SA 1.0 US CPed::ClearWeapons, verified in Entity/Ped/Ped.cpp.
    reinterpret_cast<void (__thiscall*)(CPed*)>(0x5E6320)(ped);
    return true;
}

bool ObjectTargettable(int handle)
{
    CObject* object = Object(handle);
    if (!object)
        return false;
    object->m_bIsTargetable = true;
    return true;
}

bool ObjectCollision(int handle, bool enabled)
{
    CObject* object = Object(handle);
    if (!object)
        return false;
    object->bUsesCollision = enabled;
    return true;
}

float CoordinatesDistance3D(const float* xyz)
{
    const float x = xyz[3] - xyz[0];
    const float y = xyz[4] - xyz[1];
    const float z = xyz[5] - xyz[2];
    return sqrtf(x * x + y * y + z * z);
}

}

void VcsScmBindActors(ReVcsScriptHost& host)
{
    host.characterExists = CharacterExists;
    host.vehicleExists = VehicleExists;
    host.objectExists = ObjectExists;
    host.characterAlive = CharacterAlive;
    host.characterDead = CharacterDead;
    host.characterInVehicle = CharacterInVehicle;
    host.characterOnFoot = CharacterOnFoot;
    host.characterInSpecificVehicle = CharacterInSpecificVehicle;
    host.characterInModel = CharacterInModel;
    host.characterInFlyingVehicle = CharacterInFlyingVehicle;
    host.characterInBoat = CharacterInBoat;
    host.characterOnBike = CharacterOnBike;
    host.characterVehicleNoSave = CharacterVehicleNoSave;
    host.characterCoordinates = CharacterCoordinates;
    host.characterRawCoordinates = CharacterRawCoordinates;
    host.vehicleCoordinates = VehicleCoordinates;
    host.objectCoordinates = ObjectCoordinates;
    host.characterHeadingGet = CharacterHeadingGet;
    host.vehicleHeadingQuery = VehicleHeadingGet;
    host.objectHeadingGet = ObjectHeadingGet;
    host.characterOffset = CharacterOffset;
    host.vehicleOffset = VehicleOffset;
    host.objectOffset = ObjectOffset;
    host.characterProofs = CharacterProofs;
    host.vehicleProofs = VehicleProofs;
    host.characterHealthGet = CharacterHealthGet;
    host.characterArmourGet = CharacterArmourGet;
    host.characterHealthGreater = CharacterHealthGreater;
    host.characterArmourGreater = CharacterArmourGreater;
    host.characterInWater = CharacterInWater;
    host.vehicleInWater = VehicleInWater;
    host.vehicleDead = VehicleDead;
    host.characterClearWeapons = CharacterClearWeapons;
    host.objectTargettable = ObjectTargettable;
    host.objectCollision = ObjectCollision;
    host.coordinatesDistance3D = CoordinatesDistance3D;
}
