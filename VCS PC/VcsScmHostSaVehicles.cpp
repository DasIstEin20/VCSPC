#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "Pools.h"
#include "Object.h"
#include <float.h>

namespace {
using Arg = VcsScmSaArg;

bool Create(int model, const float* xyz, bool ground, bool mission, int* handle)
{
    float position[3];
    return handle && VcsScmSaModelKind(model, 6) && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x00A5, {Arg::Int(model), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2]), Arg::Out(handle)}, nullptr, mission);
}

void Delete(int handle, bool mission)
{
    if (VcsScmSaVehicleExists(handle))
        VcsScmSaCall(0x00A6, {Arg::Int(handle)}, nullptr, mission);
}

void Release(int handle, bool mission)
{
    if (VcsScmSaVehicleExists(handle))
        VcsScmSaCall(0x01C3, {Arg::Int(handle)}, nullptr, mission);
}

void DontRemove(int handle)
{
    if (VcsScmSaVehicleExists(handle))
        VcsScmSaMissionEntity(handle, 1, false);
}

bool Idle(int handle)
{
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x00A9, {Arg::Int(handle)});
}

bool Wander(int handle)
{
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x00A8, {Arg::Int(handle)});
}

bool Freeze(int handle, bool frozen)
{
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x0519, {Arg::Int(handle), Arg::Int(frozen)});
}

bool Heading(int handle, float degrees)
{
    return degrees >= -100000.0f && degrees <= 100000.0f && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0175, {Arg::Int(handle), Arg::Float(degrees)});
}

bool FaceCoordinates(int handle, float x, float y)
{
    const float point[] = {x, y, 0.0f};
    return VcsScmSaVector(point) && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x039F, {Arg::Int(handle), Arg::Float(x), Arg::Float(y)});
}

bool Route(int handle, const float* xyz, bool ground, bool teleport)
{
    float position[3];
    if (!VcsScmSaVehicleExists(handle) || !VcsScmSaGroundPosition(xyz, ground, position))
        return false;
    if (teleport)
        return VcsScmSaCall(0x00AB, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
    return VcsScmSaCall(0x00A7, {Arg::Int(handle), Arg::Float(position[0]),
        Arg::Float(position[1]), Arg::Float(position[2])});
}

bool RouteAccurate(int handle, const float* xyz, bool ground)
{
    float position[3];
    return VcsScmSaVehicleExists(handle) && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x02C2, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
}

bool CoordinatesNoOffset(int handle, const float* xyz, bool ground)
{
    float position[3];
    return VcsScmSaVehicleExists(handle) && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x088C, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
}

bool Speed(int handle, float speed, bool cruise)
{
    if (!(speed >= -100000.0f && speed <= 100000.0f) || !VcsScmSaVehicleExists(handle))
        return false;
    if (cruise) {
        const signed char sourceSpeed = static_cast<signed char>(static_cast<int>(speed));
        return VcsScmSaCall(0x00AD, {Arg::Int(handle), Arg::Float(static_cast<float>(sourceSpeed))});
    }
    // The source setter divides by 60, while SA's forward-speed setter divides
    // by 50. The VCS getter independently uses the 50-units conversion.
    return VcsScmSaCall(0x04BA, {Arg::Int(handle), Arg::Float(speed * (50.0f / 60.0f))});
}

bool SpeedQuery(int handle, float* speed)
{
    float magnitude, velocity[3], position[3], forwardPoint[3];
    if (!speed || !VcsScmSaVehicleExists(handle) ||
        !VcsScmSaCall(0x02E3, {Arg::Int(handle), Arg::Out(&magnitude)}) ||
        !VcsScmSaCall(0x06A2, {Arg::Int(handle), Arg::Out(velocity), Arg::Out(velocity + 1), Arg::Out(velocity + 2)}) ||
        !VcsScmSaCall(0x00AA, {Arg::Int(handle), Arg::Out(position), Arg::Out(position + 1), Arg::Out(position + 2)}) ||
        !VcsScmSaCall(0x0407, {Arg::Int(handle), Arg::Float(0.0f), Arg::Float(1.0f), Arg::Float(0.0f),
            Arg::Out(forwardPoint), Arg::Out(forwardPoint + 1), Arg::Out(forwardPoint + 2)}))
        return false;
    const float dot = velocity[0] * (forwardPoint[0] - position[0]) +
        velocity[1] * (forwardPoint[1] - position[1]) + velocity[2] * (forwardPoint[2] - position[2]);
    *speed = dot < 0.0f ? -magnitude : magnitude;
    return true;
}

bool Health(int handle, int* health)
{
    return health && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0227, {Arg::Int(handle), Arg::Out(health)});
}

bool HealthGreater(int handle, int threshold, bool* result)
{
    return result && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0185, {Arg::Int(handle), Arg::Int(threshold)}, result);
}

bool HealthSet(int handle, int health)
{
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x0224, {Arg::Int(handle), Arg::Int(health)});
}

bool ColoursGet(int handle, int* colours)
{
    return colours && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x03F3, {Arg::Int(handle), Arg::Out(colours), Arg::Out(colours + 1)});
}

bool ColourPairSet(int handle, int first, int second)
{
    return first >= 0 && first < 128 && second >= 0 && second < 128 &&
        VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x0229, {
            Arg::Int(handle), Arg::Int(first), Arg::Int(second)});
}

bool Heavy(int handle, int enabled)
{
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x01EC, {Arg::Int(handle), Arg::Int(enabled != 0)});
}

bool MassCoefficients(int handle, float mass, float turnMass)
{
    CVehicle* vehicle = VcsScmSaVehicle(handle);
    if (!vehicle || !(mass > 0.0f && mass <= FLT_MAX && turnMass > 0.0f && turnMass <= FLT_MAX))
        return false;
    // Native SET_CAR_HEAVY at 0x47DDE6 verifies CVehicle + 0x384 and
    // tHandlingData + 4/+12. Replace instance masses from this shared baseline.
    const unsigned char* handling;
    memcpy(&handling, reinterpret_cast<const unsigned char*>(vehicle) + 0x384, sizeof(handling));
    if (!handling)
        return false;
    float baseMass, baseTurnMass;
    memcpy(&baseMass, handling + 4, sizeof(baseMass));
    memcpy(&baseTurnMass, handling + 12, sizeof(baseTurnMass));
    const float scaledMass = baseMass * mass;
    const float scaledTurnMass = baseTurnMass * turnMass;
    if (!(scaledMass > 0.0f && scaledMass <= FLT_MAX && scaledTurnMass > 0.0f && scaledTurnMass <= FLT_MAX))
        return false;
    vehicle->b0x01 = mass != 1.0f || turnMass != 1.0f;
    vehicle->fMass = scaledMass;
    vehicle->fTurnMass = scaledTurnMass;
    return true;
}

bool PhysicalSpeed(unsigned short sourceOpcode, int handle, const float* values)
{
    if (!VcsScmSaVector(values))
        return false;
    CPhysical* entity;
    switch (sourceOpcode) {
    case 0x0229:
    case 0x03A9: entity = VcsScmSaObject(handle); break;
    case 0x03B3:
    case 0x03B4: entity = VcsScmSaVehicle(handle); break;
    default: return false;
    }
    if (!entity)
        return false;
    const CVector scaled(values[0] * 0.02f, values[1] * 0.02f, values[2] * 0.02f);
    if (sourceOpcode == 0x0229) {
        entity->m_vecLinearVelocity.x += scaled.x;
        entity->m_vecLinearVelocity.y += scaled.y;
        entity->m_vecLinearVelocity.z += scaled.z;
    } else if (sourceOpcode == 0x03B4) {
        entity->m_vecLinearVelocity = scaled;
    } else {
        entity->m_vecAngularVelocity = scaled;
    }
    return true;
}

bool Traction(int handle, float traction)
{
    return traction >= 0.0f && traction <= 100000.0f && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0423, {Arg::Int(handle), Arg::Float(traction)});
}

bool AvoidLevelTransitions(int handle, int rawFlag)
{
    return VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0428, {Arg::Int(handle), Arg::Int(rawFlag != 0)});
}

bool DoorLock(int handle, int sourceMode)
{
    // Source modes 5/7 differ from SA's COP_CAR/SKIP_SHUT_DOORS.
    static const int modes[] = {0, 1, 2, 3, 4, -1, 6, -1};
    return sourceMode >= 0 && sourceMode < 8 && modes[sourceMode] >= 0 &&
        VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x020A, {
            Arg::Int(handle), Arg::Int(modes[sourceMode])});
}

bool TempAction(int handle, signed char sourceAction, int milliseconds)
{
    // SA inserted EMPTYTOBEREUSED at slot 2 before REVERSE.
    static const int actions[] = {0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    return sourceAction >= 0 && sourceAction < 11 && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0477, {Arg::Int(handle), Arg::Int(actions[sourceAction]), Arg::Int(milliseconds)});
}

int Driver(int handle)
{
    int driver = -1;
    if (VcsScmSaVehicleExists(handle))
        VcsScmSaCall(0x046C, {Arg::Int(handle), Arg::Out(&driver)});
    return driver;
}

bool PassengerCount(int handle, int* count)
{
    return count && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x01E9, {Arg::Int(handle), Arg::Out(count)});
}

bool MaximumPassengers(int handle, int* maximum)
{
    return maximum && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x01EA, {Arg::Int(handle), Arg::Out(maximum)});
}

bool SeatFree(int handle, int seat, bool* result)
{
    int maximum;
    if (!result || seat < -1 || !MaximumPassengers(handle, &maximum) || maximum < 0 || maximum > 8)
        return false;
    if (seat >= maximum) {
        *result = false;
        return true;
    }
    if (seat == -1) {
        int driver;
        if (!VcsScmSaCall(0x046C, {Arg::Int(handle), Arg::Out(&driver)}))
            return false;
        *result = driver == -1;
        return true;
    }
    return VcsScmSaCall(0x0431, {Arg::Int(handle), Arg::Int(seat)}, result);
}

bool Passenger(int handle, int seat, int* character)
{
    return character && seat >= 0 && seat < 8 && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x0432, {Arg::Int(handle), Arg::Int(seat), Arg::Out(character)});
}

bool SourceModel(int handle, int* source)
{
    int model = -1;
    if (!source || !VcsScmSaVehicleExists(handle) ||
        !VcsScmSaCall(0x0441, {Arg::Int(handle), Arg::Out(&model)}))
        return false;
    const int translated = VcsScmSourceByModel(model);
    if (translated < 0)
        return false;
    *source = translated;
    return true;
}

bool Forward2D(int handle, float* xy)
{
    return xy && VcsScmSaVehicleExists(handle) &&
        VcsScmSaCall(0x02F8, {Arg::Int(handle), Arg::Out(xy)}) &&
        VcsScmSaCall(0x02F9, {Arg::Int(handle), Arg::Out(xy + 1)});
}

bool Upright(int handle, bool* result)
{
    return result && VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x020D, {Arg::Int(handle)}, result);
}

bool InAirProper(int handle)
{
    bool result = false;
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x01F3, {Arg::Int(handle)}, &result) && result;
}

bool OnFire(int handle)
{
    bool result = false;
    return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x0495, {Arg::Int(handle)}, &result) && result;
}

bool Components(int, signed char first, signed char second)
{
    // Both handlers discard the model operand and set the next-instance extras.
    return first >= -2 && first < 6 && second >= -2 && second < 6 &&
        VcsScmSaCall(0x0506, {Arg::Int(0), Arg::Int(first), Arg::Int(second)});
}

bool Autopilot(unsigned short sourceOpcode, int handle, int value)
{
    if (!VcsScmSaVehicleExists(handle))
        return false;
    const unsigned char byte = static_cast<unsigned char>(value);
    switch (sourceOpcode) {
    case 0x0051:
        // Both enums' first five named driving styles have the same order.
        return byte <= 4 && VcsScmSaCall(0x00AE, {Arg::Int(handle), Arg::Int(byte)});
    case 0x0300:
        return VcsScmSaCall(0x04E0, {Arg::Int(handle), Arg::Int(byte)});
    case 0x0052:
        // NONE through BLOCKCAR_HANDBRAKESTOP are shared by the audited enums.
        // VCS ATTACKPLAYER (21) is broader than SA BOAT_ATTACKPLAYER.
        return byte <= 19 && VcsScmSaCall(0x00AF, {Arg::Int(handle), Arg::Int(byte)});
    default: return false;
    }
}
}

void VcsScmBindSaVehicles(ReVcsScriptHost& host)
{
    host.vehicleCreate = Create;
    host.vehicleDelete = Delete;
    host.vehicleRelease = Release;
    host.vehicleDontRemove = DontRemove;
    host.vehicleIdle = Idle;
    host.vehicleWander = Wander;
    host.vehicleFreeze = Freeze;
    host.vehicleHeading = Heading;
    host.vehicleFaceCoordinates = FaceCoordinates;
    host.vehicleRoute = Route;
    host.vehicleRouteAccurate = RouteAccurate;
    host.vehicleCoordinatesNoOffset = CoordinatesNoOffset;
    host.vehicleSpeed = Speed;
    host.vehicleSpeedQuery = SpeedQuery;
    host.vehicleHealth = Health;
    host.vehicleHealthGreater = HealthGreater;
    host.vehicleHealthSet = HealthSet;
    host.vehicleColoursGet = ColoursGet;
    host.vehicleColourPairSet = ColourPairSet;
    host.vehicleHeavy = Heavy;
    host.vehicleMassCoefficients = MassCoefficients;
    host.entityPhysicalSpeed = PhysicalSpeed;
    host.vehicleTraction = Traction;
    host.vehicleAvoidLevelTransitions = AvoidLevelTransitions;
    host.vehicleDoorLock = DoorLock;
    host.vehicleTempAction = TempAction;
    host.vehicleDriver = Driver;
    host.vehiclePassengerCount = PassengerCount;
    host.vehicleMaximumPassengers = MaximumPassengers;
    host.vehicleSeatFree = SeatFree;
    host.vehiclePassenger = Passenger;
    host.vehicleSourceModel = SourceModel;
    host.vehicleForward2D = Forward2D;
    host.vehicleUpright = Upright;
    host.vehicleInAirProper = InAirProper;
    host.vehicleOnFire = OnFire;
    host.vehicleComponents = Components;
    host.vehicleAutopilot = Autopilot;
}
