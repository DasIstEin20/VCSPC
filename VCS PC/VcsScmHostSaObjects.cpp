#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "ModelInfo.h"
#include "Object.h"
#include <algorithm>

namespace {
using Arg = VcsScmSaArg;

CObject* Object(int handle)
{
    return VcsScmSaObject(handle);
}

bool ObjectModel(int model)
{
    if (!VcsScmSaModelLoaded(model))
        return false;
    const unsigned char type = CModelInfo::GetModelInfo(static_cast<uint16>(model))->GetModelType();
    return type == 1 || type == 3 || type == 5;
}

bool Create(int model, float x, float y, float z, int* handle)
{
    const float xyz[] = {x, y, z};
    float position[3];
    return handle && ObjectModel(model) &&
        VcsScmSaGroundPosition(xyz, z == -100.0f || z <= -250.0f, position) &&
        VcsScmSaCall(0x029B, {Arg::Int(model), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2]), Arg::Out(handle)}, nullptr, false);
}

bool CreateWithOffset(int model, float x, float y, float z, int* handle)
{
    const float xyz[] = {x, y, z};
    float position[3];
    return handle && ObjectModel(model) &&
        VcsScmSaGroundPosition(xyz, z == -100.0f || z <= -250.0f, position) &&
        VcsScmSaCall(0x0107, {Arg::Int(model), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2]), Arg::Out(handle)}, nullptr, false);
}

bool Mission(int handle, bool mission)
{
    return VcsScmSaObjectExists(handle) && VcsScmSaMissionEntity(handle, 3, mission);
}

bool DontRemove(int handle)
{
    return VcsScmSaObjectExists(handle) && VcsScmSaCall(0x01C7, {Arg::Int(handle)});
}

bool Release(int handle, bool mission)
{
    return VcsScmSaObjectExists(handle) && VcsScmSaCall(0x01C4, {Arg::Int(handle)}, nullptr, mission);
}

void Remove(int handle)
{
    if (VcsScmSaObjectExists(handle))
        VcsScmSaCall(0x0108, {Arg::Int(handle)});
}

void ClearArea(const float* bounds)
{
    if (!bounds || !VcsScmSaVector(bounds) || !VcsScmSaVector(bounds + 3)) {
        VcsScmSaReject("CLEAR_AREA_OF_OBJECTS received invalid bounds");
        return;
    }
    const float minimum[] = {std::min(bounds[0], bounds[3]), std::min(bounds[1], bounds[4]),
        std::min(bounds[2], bounds[5])};
    const float maximum[] = {std::max(bounds[0], bounds[3]), std::max(bounds[1], bounds[4]),
        std::max(bounds[2], bounds[5])};
    for (int index = VcsScmSaObjectPoolSize() - 1; index >= 0; --index) {
        int handle;
        CObject* object = VcsScmSaObjectAtIndex(index, &handle);
        if (!object)
            continue;
        // SA CObject::CanBeDeleted at 0x59F120 excludes mission, cutscene,
        // decoration and mission2. Map dummies remain owned by the map loader.
        const unsigned char type = object->m_nObjectType;
        if (type == 2 || type == 4 || type == 5 || type == 6)
            continue;
        const CVector& point = object->GetCoords();
        if (point.x < minimum[0] || point.x > maximum[0] || point.y < minimum[1] ||
            point.y > maximum[1] || point.z < minimum[2] || point.z > maximum[2])
            continue;
        if (!VcsScmSaCall(0x0108, {Arg::Int(handle)}, nullptr, false))
            return;
    }
}

bool Heading(int handle, float degrees)
{
    return degrees >= -20000000.0f && degrees <= 20000000.0f &&
        VcsScmSaObjectExists(handle) && VcsScmSaCall(0x0177, {Arg::Int(handle), Arg::Float(degrees)});
}

bool CoordinatesSet(int handle, const float* xyz, bool ground)
{
    float position[3];
    return VcsScmSaObjectExists(handle) && VcsScmSaGroundPosition(xyz, ground, position) &&
        VcsScmSaCall(0x01BC, {Arg::Int(handle), Arg::Float(position[0]),
            Arg::Float(position[1]), Arg::Float(position[2])});
}

bool Velocity(int handle, const float* velocity)
{
    return VcsScmSaVector(velocity) && VcsScmSaObjectExists(handle) &&
        VcsScmSaCall(0x0381, {Arg::Int(handle), Arg::Float(velocity[0]),
            Arg::Float(velocity[1]), Arg::Float(velocity[2])});
}

bool Dynamic(int handle, bool enabled)
{
    return VcsScmSaObjectExists(handle) && VcsScmSaCall(0x0392, {Arg::Int(handle), Arg::Int(enabled)});
}

bool Mass(int handle, const float* values)
{
    CObject* object = Object(handle);
    if (!object || !values || !(values[0] > 0.0f && values[0] <= 1e10f &&
        values[1] > 0.0f && values[1] <= 1e12f && values[2] >= 0.0f && values[2] <= 10000.0f))
        return false;
    if (!VcsScmSaCall(0x0906, {Arg::Int(handle), Arg::Float(values[0])}) ||
        !VcsScmSaCall(0x0908, {Arg::Int(handle), Arg::Float(values[1])}))
        return false;
    object->m_fAirResistance = values[2];
    // SA CPhysical's INFINITE_MASS bit is 0x40 in General.h's ABI view.
    const bool infinite = values[0] >= 99998.0f;
    object->b0x40 = infinite;
    object->bApplyGravity = !infinite;
    return true;
}

bool SeparateRender(int handle)
{
    return VcsScmSaObjectExists(handle) && VcsScmSaCall(0x0418, {Arg::Int(handle), Arg::Int(1)});
}

bool PhysicalFlags(unsigned short sourceOpcode, int handle, const int* flags)
{
    if (!flags || !VcsScmSaObjectExists(handle))
        return false;
    switch (sourceOpcode) {
    case 0x033D: return VcsScmSaCall(0x0550, {Arg::Int(handle), Arg::Int(flags[0] != 0)});
    case 0x02FA: return VcsScmSaCall(0x04D9, {Arg::Int(handle), Arg::Int(flags[0] != 0)});
    case 0x03B5:
        return VcsScmSaCall(0x09CA, {Arg::Int(handle), Arg::Int(flags[0] != 0),
            Arg::Int(flags[1] != 0), Arg::Int(flags[2] != 0), Arg::Int(flags[3] != 0), Arg::Int(flags[4] != 0)});
    case 0x04BA: {
        CObject* object = Object(handle);
        if (!object)
            return false;
        object->byCollisionRecords = 0;
        return true;
    }
    default: return false;
    }
}

bool PhysicalQuery(unsigned short sourceOpcode, int handle, bool* result)
{
    if (!result || !VcsScmSaObjectExists(handle))
        return false;
    switch (sourceOpcode) {
    case 0x02FB: return VcsScmSaCall(0x04DA, {Arg::Int(handle)}, result);
    case 0x0306: return VcsScmSaCall(0x04E7, {Arg::Int(handle)}, result);
    default: return false;
    }
}

bool CharacterTouch(int character, int handle, bool* result)
{
    return result && VcsScmSaPedExists(character) && VcsScmSaObjectExists(handle) &&
        VcsScmSaCall(0x0179, {Arg::Int(character), Arg::Int(handle)}, result);
}

bool VehicleTouch(int handle, int vehicle, bool* result)
{
    return result && VcsScmSaObjectExists(handle) && VcsScmSaVehicleExists(vehicle) &&
        VcsScmSaCall(0x0897, {Arg::Int(vehicle), Arg::Int(handle)}, result);
}

bool Rotate(int handle, float target, float step, bool collision, bool* finished)
{
    const float angles[] = {target, step, 0.0f};
    return finished && VcsScmSaVector(angles) && VcsScmSaObjectExists(handle) &&
        VcsScmSaCall(0x034D, {Arg::Int(handle), Arg::Float(target), Arg::Float(step), Arg::Int(collision)}, finished);
}

bool Slide(int handle, const float* target, const float* step, bool collision, bool* finished)
{
    return finished && VcsScmSaVector(target) && VcsScmSaVector(step) && VcsScmSaObjectExists(handle) &&
        VcsScmSaCall(0x034E, {Arg::Int(handle), Arg::Float(target[0]), Arg::Float(target[1]),
            Arg::Float(target[2]), Arg::Float(step[0]), Arg::Float(step[1]), Arg::Float(step[2]),
            Arg::Int(collision)}, finished);
}

bool SortVehicleCollision(int handle, int vehicle)
{
    CObject* object = Object(handle);
    if (!object)
        return false;
    if (VcsScmSaVehicleExists(vehicle))
        return VcsScmSaCall(0x050E, {Arg::Int(handle), Arg::Int(vehicle)});
    // Verified 0x48CB16 stores the car pointer in CPhysical's ignored entity.
    // Clear it directly because native GetAt(-1) reads before the pool array.
    memset(object->pad3 + 16, 0, 4);
    return true;
}

bool Attachment(unsigned short sourceOpcode, int handle, int vehicle, const float* offset)
{
    if (!VcsScmSaObjectExists(handle))
        return false;
    if (sourceOpcode == 0x03E7)
        return VcsScmSaVector(offset) && VcsScmSaVehicleExists(vehicle) && VcsScmSaCall(0x0681, {
            Arg::Int(handle), Arg::Int(vehicle), Arg::Float(offset[0]), Arg::Float(offset[1]),
            Arg::Float(offset[2]), Arg::Float(0.0f), Arg::Float(0.0f), Arg::Float(0.0f)});
    if (sourceOpcode == 0x03E8 || sourceOpcode == 0x04C1)
        return VcsScmSaCall(0x0682, {Arg::Int(handle), Arg::Float(0.0f),
            Arg::Float(0.0f), Arg::Float(0.0f), Arg::Int(0)});
    return false;
}

bool DamageSet(unsigned short sourceOpcode, int handle, int rawValue, int)
{
    CObject* object = Object(handle);
    if (!object)
        return false;
    if (sourceOpcode == 0x03BE) {
        object->bRenderDamaged = true;
        return true;
    }
    if (sourceOpcode == 0x0490) {
        const unsigned int byte = static_cast<unsigned int>(rawValue) & 255u;
        const int health = byte < 128 ? static_cast<int>(byte) : static_cast<int>(byte) - 256;
        if (!VcsScmSaCall(0x071F, {Arg::Int(handle), Arg::Int(health)}))
            return false;
        object->bUsesCollision = true;
        object->bExplosionProof = false;
        return true;
    }
    // VCS 0475's per-instance damage multiplier needs a native damage hook.
    return false;
}

bool DamageQuery(unsigned short sourceOpcode, int handle, bool* result)
{
    CObject* object = Object(handle);
    if (!result)
        return false;
    if (sourceOpcode == 0x04D8) {
        *result = object && object->m_fHealth == 0.0f;
        return true;
    }
    if (sourceOpcode == 0x021A && object) {
        *result = object->bRenderDamaged || !object->bIsVisible;
        return true;
    }
    return false;
}

bool Locate(bool three, int handle, const float* center, const float* radius, bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(center) || !VcsScmSaVector(radius) || !VcsScmSaObjectExists(handle))
        return false;
    bool success;
    if (three)
        success = VcsScmSaCall(0x04E6, {Arg::Int(handle), Arg::Float(center[0]), Arg::Float(center[1]),
            Arg::Float(center[2]), Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]),
            Arg::Int(0)}, result);
    else
        success = VcsScmSaCall(0x04E5, {Arg::Int(handle), Arg::Float(center[0]), Arg::Float(center[1]),
            Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)}, result);
    if (!success || !marker)
        return success;
    float position[3] = {};
    if (!three && !VcsScmSaCall(0x01BB, {Arg::Int(handle), Arg::Out(position), Arg::Out(position + 1),
        Arg::Out(position + 2)}))
        return false;
    return VcsScmSaDrawLocateMarker(markerId, center, radius, three, position[2]);
}

bool OnScreen(unsigned short sourceOpcode, int handle, bool* result)
{
    if (!result)
        return false;
    switch (sourceOpcode) {
    case 0x01B7:
        return VcsScmSaVehicleExists(handle) && VcsScmSaCall(0x02CA, {Arg::Int(handle)}, result);
    case 0x01B8:
        return VcsScmSaPedExists(handle) && VcsScmSaCall(0x02CB, {Arg::Int(handle)}, result);
    case 0x01B9:
        return VcsScmSaObjectExists(handle) && VcsScmSaCall(0x02CC, {Arg::Int(handle)}, result);
    default: return false;
    }
}
}

void VcsScmBindSaObjects(ReVcsScriptHost& host)
{
    host.objectCreate = Create;
    host.objectCreateWithOffset = CreateWithOffset;
    host.objectMission = Mission;
    host.objectDontRemove = DontRemove;
    host.objectRelease = Release;
    host.objectRemove = Remove;
    host.clearObjectsArea = ClearArea;
    host.objectHeading = Heading;
    host.objectCoordinatesSet = CoordinatesSet;
    host.objectVelocity = Velocity;
    host.objectDynamic = Dynamic;
    host.objectMass = Mass;
    host.objectSeparateRender = SeparateRender;
    host.objectPhysicalFlags = PhysicalFlags;
    host.objectPhysicalQuery = PhysicalQuery;
    host.characterTouchObject = CharacterTouch;
    host.objectTouchVehicle = VehicleTouch;
    host.objectRotate = Rotate;
    host.objectSlide = Slide;
    host.objectSortVehicleCollision = SortVehicleCollision;
    host.objectAttachment = Attachment;
    host.objectDamageSet = DamageSet;
    host.objectDamageQuery = DamageQuery;
    host.objectLocate = Locate;
    host.entityOnScreen = OnScreen;
}
