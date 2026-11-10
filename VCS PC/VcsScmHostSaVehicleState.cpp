#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "Pools.h"

namespace {
using Arg = VcsScmSaArg;

CVehicle* Vehicle(int handle)
{
    CVehiclePool* pool = CPools::GetVehiclePool();
    return pool && handle >= 0 && !(handle & 0x80) && pool->IsValid(handle >> 8) ? pool->GetAt(handle) : nullptr;
}

bool IsBike(int handle, bool* result)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle || !result)
        return false;
    *result = vehicle->GetSubClass() == VEHICLE_BIKE;
    return true;
}

bool SourceType(unsigned short sourceOpcode, int handle, bool* result)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle || !result)
        return false;
    const eVehicleType type = vehicle->GetSubClass();
    switch (sourceOpcode) {
    case 0x01AB: *result = type == VEHICLE_BOAT; break;
    case 0x037E: *result = type == VEHICLE_PLANE || type == VEHICLE_FPLANE; break;
    case 0x037F: *result = type == VEHICLE_HELI || type == VEHICLE_FHELI; break;
    case 0x037C: {
        const int source = VcsScmSourceByModel(vehicle->GetModelIndex());
        if (source < 0)
            return false;
        const unsigned int narrowed = static_cast<unsigned int>(static_cast<int>(static_cast<short>(source)));
        *result = narrowed >= 170 && narrowed <= 0xfffffc4cu;
        break;
    }
    default: return false;
    }
    return true;
}

bool StateQuery(unsigned short sourceOpcode, int handle, bool* result)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle || !result)
        return false;
    switch (sourceOpcode) {
    case 0x0111: return VcsScmSaCall(0x01C1, {Arg::Int(handle)}, result);
    case 0x0251: return VcsScmSaCall(0x03C9, {Arg::Int(handle)}, result);
    case 0x0424: *result = vehicle->GetVehicleFlags().bSirenOrAlarm; return true;
    default: return false;
    }
}

bool StateSet(unsigned short sourceOpcode, int handle, int value)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle)
        return false;
    const int enabled = value != 0;
    const bool automobile = vehicle->GetClass() == VEHICLE_AUTOMOBILE;
    switch (sourceOpcode) {
    case 0x0201: return VcsScmSaCall(0x0338, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x023D: return VcsScmSaCall(0x03AB, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x022E: return VcsScmSaCall(0x0397, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x0230: return VcsScmSaCall(0x039C, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x026D: return automobile && VcsScmSaCall(0x03ED, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x0142: return automobile && VcsScmSaCall(0x0216, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x0236: {
        // PLAYER through REMOTE_CONTROLLED share their audited source names.
        const unsigned int status = static_cast<unsigned int>(value) & 31u;
        return status <= 8 && VcsScmSaCall(0x03A2, {Arg::Int(handle), Arg::Int(status)});
    }
    default: return false;
    }
}

bool CheckCommand(unsigned short sourceOpcode, int handle, float radius, unsigned int milliseconds)
{
    if (!VcsScmSaVehicleExists(handle))
        return false;
    switch (sourceOpcode) {
    case 0x00DD: return VcsScmSaCall(0x0190, {Arg::Int(handle)});
    case 0x00DE: return VcsScmSaCall(0x0191, {Arg::Int(handle)});
    case 0x0254:
        return radius >= 0.0f && radius <= 20000000.0f &&
            VcsScmSaCall(0x03CC, {Arg::Int(handle), Arg::Float(radius), Arg::Int(static_cast<int>(milliseconds))});
    case 0x0255: return VcsScmSaCall(0x03CD, {Arg::Int(handle)});
    default: return false;
    }
}

bool CheckQuery(unsigned short sourceOpcode, int handle, bool* result)
{
    if (!result || !VcsScmSaVehicleExists(handle))
        return false;
    switch (sourceOpcode) {
    case 0x00DC: return VcsScmSaCall(0x018F, {Arg::Int(handle)}, result);
    case 0x012D: return VcsScmSaCall(0x01F4, {Arg::Int(handle)}, result);
    case 0x0256: return VcsScmSaCall(0x03CE, {Arg::Int(handle)}, result);
    default: return false;
    }
}

bool BoatCommand(unsigned short sourceOpcode, int handle, const float* values)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!values || !vehicle || vehicle->GetSubClass() != VEHICLE_BOAT)
        return false;
    switch (sourceOpcode) {
    case 0x01F2:
        return VcsScmSaCall(0x0323, {Arg::Int(handle), Arg::Int(values[0] != 0.0f)});
    case 0x01C3:
        if (!(values[0] >= -100000.0f && values[0] <= 100000.0f))
            return false;
        return VcsScmSaCall(0x02DB, {Arg::Int(handle),
            Arg::Float(static_cast<float>(static_cast<signed char>(static_cast<int>(values[0]))))});
    case 0x01BF:
        // SA resolves its own water-height sentinel in this command.
        return VcsScmSaVector(values) && VcsScmSaCall(0x02D3, {Arg::Int(handle),
            Arg::Float(values[0]), Arg::Float(values[1]),
            Arg::Float(values[2] == -100.0f || values[2] <= -250.0f ? -1000.0f : values[2])});
    case 0x01C0: return VcsScmSaCall(0x02D4, {Arg::Int(handle)});
    default: return false;
    }
}

bool Helicopter(unsigned short sourceOpcode, int handle, const float* values, int raw)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle || vehicle->GetSubClass() != VEHICLE_HELI)
        return false;
    switch (sourceOpcode) {
    case 0x02F2:
        return values && values[0] >= -1000000.0f && values[0] <= 1000000.0f &&
            VcsScmSaCall(0x04D0, {Arg::Int(handle), Arg::Float(values[0])});
    case 0x02F3: return VcsScmSaCall(0x04D1, {Arg::Int(handle)});
    case 0x02FF: return VcsScmSaCall(0x04DF, {Arg::Int(handle), Arg::Int(raw != 0)});
    case 0x03D8: return VcsScmSaCall(0x0825, {Arg::Int(handle)});
    // VCS's one-byte cruise argument has no equivalent in SA's two-altitude
    // HELI_GOTO_COORDS signature. It requires an explicit flight-policy owner.
    default: return false;
    }
}

bool FireHunterGun(int handle)
{
    CVehicle* vehicle = Vehicle(handle);
    return vehicle && vehicle->GetSubClass() == VEHICLE_HELI &&
        VcsScmSaCall(0x0541, {Arg::Int(handle)});
}

bool DamageQuery(unsigned short sourceOpcode, int handle, int selector, bool* result)
{
    if (!result || !VcsScmSaVehicleExists(handle))
        return false;
    if (sourceOpcode == 0x01EF) {
        const int type = VcsScmSaDamageType(selector);
        return type >= 0 && VcsScmSaCall(0x031E, {Arg::Int(handle), Arg::Int(type)}, result);
    }
    if (sourceOpcode == 0x02D9)
        return selector >= 0 && selector <= 4 &&
            VcsScmSaCall(0x0496, {Arg::Int(handle), Arg::Int(selector)}, result);
    return false;
}

bool DamageCommand(unsigned short sourceOpcode, int handle, int value)
{
    CVehicle* vehicle = Vehicle(handle);
    if (!vehicle)
        return sourceOpcode == 0x02BB;
    const bool automobile = vehicle->GetClass() == VEHICLE_AUTOMOBILE;
    switch (sourceOpcode) {
    case 0x0275: return VcsScmSaCall(0x03F5, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x01B0: return VcsScmSaCall(0x02AA, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x013B:
        return VcsScmSaCall(0x03F5, {Arg::Int(handle), Arg::Int(1)}) &&
            VcsScmSaCall(0x020B, {Arg::Int(handle)});
    case 0x0334: return VcsScmSaCall(0x053F, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x031C: return automobile && VcsScmSaCall(0x0508, {Arg::Int(handle)});
    case 0x02BB: return VcsScmSaCall(0x0468, {Arg::Int(handle)});
    case 0x0301: return automobile && VcsScmSaCall(0x04E1, {Arg::Int(handle)});
    case 0x0533: {
        // VCS order is right-front, right-rear, left-front, left-rear.
        static const int doors[] = {3, 5, 2, 4};
        return automobile && value >= 0 && value < 4 &&
            VcsScmSaCall(0x0689, {Arg::Int(handle), Arg::Int(doors[value]), Arg::Int(0)});
    }
    default: return false;
    }
}
}

void VcsScmBindSaVehicleState(ReVcsScriptHost& host)
{
    host.vehicleIsBike = IsBike;
    host.vehicleSourceType = SourceType;
    host.vehicleStateQuery = StateQuery;
    host.vehicleStateSet = StateSet;
    host.vehicleCheckCommand = CheckCommand;
    host.vehicleCheckQuery = CheckQuery;
    host.boatCommand = BoatCommand;
    host.vehicleHelicopter = Helicopter;
    host.vehicleFireHunterGun = FireHunterGun;
    host.vehicleDamageQuery = DamageQuery;
    host.vehicleDamageCommand = DamageCommand;
}
