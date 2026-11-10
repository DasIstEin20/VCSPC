#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"

namespace {
using Arg = VcsScmSaArg;

bool Create(int sourceType, int model, const float* xyz, bool ground, bool mission, int* handle)
{
    const int type = VcsScmSaPedType(sourceType);
    float position[3];
    return handle && type >= 0 && VcsScmSaModelKind(model, 7) &&
        VcsScmSaGroundPosition(xyz, ground, position) && VcsScmSaCall(0x009A, {
            Arg::Int(type), Arg::Int(model), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2]), Arg::Out(handle)}, nullptr, mission);
}

bool CreateDriver(int vehicle, int sourceType, int model, bool mission, int* handle)
{
    const int type = VcsScmSaPedType(sourceType);
    return handle && type >= 0 && VcsScmSaVehicleExists(vehicle) &&
        VcsScmSaModelKind(model, 7) && VcsScmSaCall(0x0129, {
            Arg::Int(vehicle), Arg::Int(type), Arg::Int(model), Arg::Out(handle)}, nullptr, mission);
}

bool CreatePassenger(int vehicle, int sourceType, int model, int seat, bool mission, int* handle)
{
    const int type = VcsScmSaPedType(sourceType);
    return handle && seat >= -1 && seat < 8 && type >= 0 &&
        VcsScmSaVehicleExists(vehicle) && VcsScmSaModelKind(model, 7) &&
        VcsScmSaCall(0x01C8, {Arg::Int(vehicle), Arg::Int(type), Arg::Int(model),
            Arg::Int(seat), Arg::Out(handle)}, nullptr, mission);
}

bool CreateRandomDriver(int vehicle, bool mission, int* handle)
{
    return handle && VcsScmSaVehicleExists(vehicle) && VcsScmSaCall(0x0560, {
        Arg::Int(vehicle), Arg::Out(handle)}, nullptr, mission);
}

bool CreateRandomPassenger(int vehicle, int seat, bool mission, int* handle)
{
    return handle && seat >= -1 && seat < 8 && VcsScmSaVehicleExists(vehicle) &&
        VcsScmSaCall(0x0561, {Arg::Int(vehicle), Arg::Int(seat), Arg::Out(handle)}, nullptr, mission);
}

bool CreateRandom(const float* xyz, bool ground, bool mission, int* handle)
{
    float position[3];
    return handle && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x0376, {Arg::Float(position[0]), Arg::Float(position[1]),
            Arg::Float(position[2]), Arg::Out(handle)}, nullptr, mission);
}

void Delete(int handle, bool mission)
{
    if (VcsScmSaPedExists(handle))
        VcsScmSaCall(0x009B, {Arg::Int(handle)}, nullptr, mission);
}

void Release(unsigned short sourceOpcode, int handle, bool mission)
{
    if (!VcsScmSaPedExists(handle))
        return;
    switch (sourceOpcode) {
    case 0x0112:
        VcsScmSaCall(0x01C2, {Arg::Int(handle)}, nullptr, mission);
        break;
    case 0x020D:
        VcsScmSaCall(0x034F, {Arg::Int(handle)}, nullptr, mission);
        break;
    }
}

void DontRemove(int handle)
{
    if (VcsScmSaPedExists(handle))
        VcsScmSaCall(0x01C5, {Arg::Int(handle)});
}

bool StoreVehicle(int handle, bool mission, int* vehicle)
{
    return vehicle && VcsScmSaPedExists(handle) && VcsScmSaCall(0x00D9, {
        Arg::Int(handle), Arg::Out(vehicle)}, nullptr, mission);
}

bool CoordinatesSet(int handle, const float* xyz, bool ground, bool noOffset)
{
    float position[3];
    if (!VcsScmSaPedExists(handle) || !VcsScmSaGroundPosition(xyz, ground, position))
        return false;
    if (noOffset)
        return VcsScmSaCall(0x09BC, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
    return VcsScmSaCall(0x08C7, {Arg::Int(handle), Arg::Float(position[0]),
        Arg::Float(position[1]), Arg::Float(position[2])});
}

bool Heading(int handle, float degrees)
{
    return degrees >= -100000.0f && degrees <= 100000.0f &&
        VcsScmSaPedExists(handle) && VcsScmSaCall(0x0173, {Arg::Int(handle), Arg::Float(degrees)});
}

bool Health(int handle, int health)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x0223, {Arg::Int(handle), Arg::Int(health)});
}

bool ArmourAdd(int handle, int amount)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x035F, {Arg::Int(handle), Arg::Int(amount)});
}

bool MoneySet(int handle, int amount)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x03FE, {
        Arg::Int(handle), Arg::Int(static_cast<unsigned short>(amount))});
}

bool Freeze(int handle, bool frozen)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x04D7, {Arg::Int(handle), Arg::Int(frozen)});
}

bool Flags(int handle, bool visibility, bool enabled)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    if (visibility)
        return VcsScmSaCall(0x0337, {Arg::Int(handle), Arg::Int(enabled)});
    return VcsScmSaCall(0x0619, {Arg::Int(handle), Arg::Int(enabled)});
}

bool NeverTargetted(int handle, bool enabled)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x0568, {Arg::Int(handle), Arg::Int(enabled)});
}

bool ShutUp(int handle, bool silent)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    if (silent)
        return VcsScmSaCall(0x094E, {Arg::Int(handle), Arg::Int(1)});
    return VcsScmSaCall(0x094F, {Arg::Int(handle)});
}

bool Mobile(int handle, int flag)
{
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x0729, {Arg::Int(handle), Arg::Int(flag != 0)});
}

bool InZone(int handle, const char* label, bool* result)
{
    return label && result && VcsScmSaPedExists(handle) &&
        VcsScmSaCall(0x0154, {Arg::Int(handle), Arg::Text(label)}, result);
}

bool Stopped(int handle)
{
    bool result = false;
    return VcsScmSaPedExists(handle) && VcsScmSaCall(0x02A0, {Arg::Int(handle)}, &result) && result;
}

bool Sitting(int handle, int vehicle, bool specific, bool* result)
{
    if (!result || !VcsScmSaPedExists(handle))
        return false;
    if (specific)
        return VcsScmSaVehicleExists(vehicle) &&
            VcsScmSaCall(0x0448, {Arg::Int(handle), Arg::Int(vehicle)}, result);
    return VcsScmSaCall(0x0449, {Arg::Int(handle)}, result);
}

bool WarpIntoVehicle(int handle, int vehicle, int seat)
{
    if (!VcsScmSaPedExists(handle) || !VcsScmSaVehicleExists(vehicle) || seat < -1 || seat >= 8)
        return false;
    if (seat == -1)
        return VcsScmSaCall(0x036A, {Arg::Int(handle), Arg::Int(vehicle)});
    return VcsScmSaCall(0x0430, {Arg::Int(handle), Arg::Int(vehicle), Arg::Int(seat)});
}

bool WarpFromVehicle(int handle, const float* xyz, bool ground)
{
    float position[3];
    return VcsScmSaPedExists(handle) && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x0362, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
}

bool AttachVehicle(int handle, int vehicle, const float* offset,
    unsigned short direction, float degrees, int sourceWeapon)
{
    const int weapon = VcsScmSaWeaponType(sourceWeapon);
    return weapon >= 0 && direction <= 3 && VcsScmSaVector(offset) &&
        VcsScmSaPedExists(handle) && VcsScmSaVehicleExists(vehicle) &&
        VcsScmSaCall(0x0464, {Arg::Int(handle), Arg::Int(vehicle),
            Arg::Float(offset[0]), Arg::Float(offset[1]), Arg::Float(offset[2]),
            Arg::Int(direction), Arg::Float(degrees), Arg::Int(weapon)});
}

bool AttachObject(int handle, int object, const float* offset,
    unsigned short direction, float degrees, int sourceWeapon)
{
    const int weapon = VcsScmSaWeaponType(sourceWeapon);
    return weapon >= 0 && direction <= 3 && VcsScmSaVector(offset) &&
        VcsScmSaPedExists(handle) && VcsScmSaObjectExists(object) &&
        VcsScmSaCall(0x04F4, {Arg::Int(handle), Arg::Int(object),
            Arg::Float(offset[0]), Arg::Float(offset[1]), Arg::Float(offset[2]),
            Arg::Int(direction), Arg::Float(degrees), Arg::Int(weapon)});
}

void DetachVehicle(int handle)
{
    if (VcsScmSaPedExists(handle))
        VcsScmSaCall(0x0465, {Arg::Int(handle)});
}

bool Identity(unsigned short sourceOpcode, int handle, int value, bool* result)
{
    if (!result || !VcsScmSaPedExists(handle))
        return false;
    if (sourceOpcode == 0x01D0) {
        const int model = VcsScmModelBySource(value);
        return model >= 0 && VcsScmSaCall(0x02F2, {Arg::Int(handle), Arg::Int(model)}, result);
    }
    if (sourceOpcode == 0x042D) {
        const int wanted = VcsScmSaPedType(value);
        int actual = -1;
        if (wanted < 0 || !VcsScmSaCall(0x089F, {Arg::Int(handle), Arg::Out(&actual)}))
            return false;
        *result = actual == wanted;
        return true;
    }
    return false;
}
}

void VcsScmBindSaPeds(ReVcsScriptHost& host)
{
    host.characterCreate = Create;
    host.characterCreateDriver = CreateDriver;
    host.characterCreatePassenger = CreatePassenger;
    host.characterCreateRandomDriver = CreateRandomDriver;
    host.characterCreateRandomPassenger = CreateRandomPassenger;
    host.randomCharacterCreate = CreateRandom;
    host.characterDelete = Delete;
    host.characterRelease = Release;
    host.characterDontRemove = DontRemove;
    host.characterStoreVehicle = StoreVehicle;
    host.characterCoordinatesSet = CoordinatesSet;
    host.characterHeading = Heading;
    host.characterHealth = Health;
    host.characterArmourAdd = ArmourAdd;
    host.characterMoneySet = MoneySet;
    host.characterFreeze = Freeze;
    host.characterFlags = Flags;
    host.characterNeverTargetted = NeverTargetted;
    host.characterShutUp = ShutUp;
    host.characterMobile = Mobile;
    host.characterInZone = InZone;
    host.characterStopped = Stopped;
    host.characterSitting = Sitting;
    host.characterWarpIntoVehicle = WarpIntoVehicle;
    host.characterWarpFromVehicle = WarpFromVehicle;
    host.characterAttachVehicle = AttachVehicle;
    host.characterAttachObject = AttachObject;
    host.characterDetachVehicle = DetachVehicle;
    host.characterIdentity = Identity;
}
