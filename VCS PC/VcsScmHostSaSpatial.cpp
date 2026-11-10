#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"

namespace {
using Arg = VcsScmSaArg;

bool PedMarker(int handle, unsigned int id, const float* center, const float* radius, bool three)
{
    float position[3] = {};
    if (!three && !VcsScmSaCall(0x00A0, {Arg::Int(handle),
        Arg::Out(position), Arg::Out(position + 1), Arg::Out(position + 2)}))
        return false;
    return VcsScmSaDrawLocateMarker(id, center, radius, three, position[2]);
}

bool CharacterLocate(unsigned short sourceOpcode, int handle, const float* center,
    const float* radius, bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(center) || !VcsScmSaVector(radius) || !VcsScmSaPedExists(handle))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Float(center[0]),
        Arg::Float(center[1]), Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Float(center[0]), Arg::Float(center[1]),
        Arg::Float(center[2]), Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x0080: success = VcsScmSaCall(0x00EC, two, result); break;
    case 0x0081: success = VcsScmSaCall(0x00ED, two, result); break;
    case 0x0082: success = VcsScmSaCall(0x00EE, two, result); break;
    case 0x0083: success = VcsScmSaCall(0x00EF, two, result); break;
    case 0x0084: success = VcsScmSaCall(0x00F0, two, result); break;
    case 0x0085: success = VcsScmSaCall(0x00F1, two, result); break;
    case 0x0089: success = VcsScmSaCall(0x00FE, three, result); break;
    case 0x008A: success = VcsScmSaCall(0x00FF, three, result); break;
    case 0x008B: success = VcsScmSaCall(0x0100, three, result); break;
    case 0x008C: success = VcsScmSaCall(0x0101, three, result); break;
    case 0x008D: success = VcsScmSaCall(0x0102, three, result); break;
    case 0x008E: success = VcsScmSaCall(0x0103, three, result); break;
    default: return false;
    }
    return success && (!marker || PedMarker(handle, markerId, center, radius, sourceOpcode >= 0x0089));
}

bool CharacterArea(unsigned short sourceOpcode, int handle, const float* first,
    const float* second, bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(first) || !VcsScmSaVector(second) || !VcsScmSaPedExists(handle))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Float(first[0]),
        Arg::Float(first[1]), Arg::Float(second[0]), Arg::Float(second[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Float(first[0]), Arg::Float(first[1]),
        Arg::Float(first[2]), Arg::Float(second[0]), Arg::Float(second[1]), Arg::Float(second[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x0046: success = VcsScmSaCall(0x00A3, two, result); break;
    case 0x0047: success = VcsScmSaCall(0x00A4, three, result); break;
    case 0x00F6: success = VcsScmSaCall(0x01A1, two, result); break;
    case 0x00F7: success = VcsScmSaCall(0x01A2, two, result); break;
    case 0x00F8: success = VcsScmSaCall(0x01A3, two, result); break;
    case 0x00F9: success = VcsScmSaCall(0x01A4, two, result); break;
    case 0x00FA: success = VcsScmSaCall(0x01A5, two, result); break;
    case 0x00FB: success = VcsScmSaCall(0x01A6, three, result); break;
    case 0x00FC: success = VcsScmSaCall(0x01A7, three, result); break;
    case 0x00FD: success = VcsScmSaCall(0x01A8, three, result); break;
    case 0x00FE: success = VcsScmSaCall(0x01A9, three, result); break;
    case 0x00FF: success = VcsScmSaCall(0x01AA, three, result); break;
    case 0x01C1: success = VcsScmSaCall(0x02D6, two, result); break;
    default: return false;
    }
    if (!success || !marker)
        return success;
    const float center[] = {(first[0] + second[0]) * 0.5f, (first[1] + second[1]) * 0.5f,
        (first[2] + second[2]) * 0.5f};
    const float radius[] = {fabsf(second[0] - first[0]) * 0.5f, fabsf(second[1] - first[1]) * 0.5f,
        fabsf(second[2] - first[2]) * 0.5f};
    const bool threeDimensional = sourceOpcode == 0x0047 || (sourceOpcode >= 0x00FB && sourceOpcode <= 0x00FF);
    return PedMarker(handle, markerId, center, radius, threeDimensional);
}

bool LocateCharacter(unsigned short sourceOpcode, int handle, int target, const float* radius,
    bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(radius) || !VcsScmSaPedExists(handle) || !VcsScmSaPedExists(target))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x0086: success = VcsScmSaCall(0x00F2, two, result); break;
    case 0x0087: success = VcsScmSaCall(0x00F3, two, result); break;
    case 0x0088: success = VcsScmSaCall(0x00F4, two, result); break;
    case 0x008F: success = VcsScmSaCall(0x0104, three, result); break;
    case 0x0090: success = VcsScmSaCall(0x0105, three, result); break;
    case 0x0091: success = VcsScmSaCall(0x0106, three, result); break;
    default: return false;
    }
    if (!success || !marker)
        return success;
    float center[3];
    return VcsScmSaCall(0x00A0, {Arg::Int(target), Arg::Out(center), Arg::Out(center + 1), Arg::Out(center + 2)}) &&
        PedMarker(handle, markerId, center, radius, sourceOpcode >= 0x008F);
}

bool LocateVehicle(unsigned short sourceOpcode, int handle, int target, const float* radius,
    bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(radius) || !VcsScmSaPedExists(handle) || !VcsScmSaVehicleExists(target))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x0132: success = VcsScmSaCall(0x0202, two, result); break;
    case 0x0133: success = VcsScmSaCall(0x0203, two, result); break;
    case 0x0134: success = VcsScmSaCall(0x0204, two, result); break;
    case 0x0135: success = VcsScmSaCall(0x0205, three, result); break;
    case 0x0136: success = VcsScmSaCall(0x0206, three, result); break;
    case 0x0137: success = VcsScmSaCall(0x0207, three, result); break;
    default: return false;
    }
    if (!success || !marker)
        return success;
    float center[3];
    return VcsScmSaCall(0x00AA, {Arg::Int(target), Arg::Out(center), Arg::Out(center + 1), Arg::Out(center + 2)}) &&
        PedMarker(handle, markerId, center, radius, sourceOpcode >= 0x0135);
}

bool LocateObject(unsigned short sourceOpcode, int handle, int target, const float* radius,
    bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(radius) || !VcsScmSaPedExists(handle) || !VcsScmSaObjectExists(target))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Int(target),
        Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x02C1: success = VcsScmSaCall(0x0471, two, result); break;
    case 0x02C2: success = VcsScmSaCall(0x0472, two, result); break;
    case 0x02C3: success = VcsScmSaCall(0x0473, two, result); break;
    case 0x02C4: success = VcsScmSaCall(0x0474, three, result); break;
    case 0x02C5: success = VcsScmSaCall(0x0475, three, result); break;
    case 0x02C6: success = VcsScmSaCall(0x0476, three, result); break;
    default: return false;
    }
    if (!success || !marker)
        return success;
    float center[3];
    return VcsScmSaCall(0x01BB, {Arg::Int(target), Arg::Out(center), Arg::Out(center + 1), Arg::Out(center + 2)}) &&
        PedMarker(handle, markerId, center, radius, sourceOpcode >= 0x02C4);
}

bool VehicleLocate(unsigned short sourceOpcode, int handle, const float* center,
    const float* radius, bool marker, unsigned int markerId, bool* result)
{
    if (!result || !VcsScmSaVector(center) || !VcsScmSaVector(radius) || !VcsScmSaVehicleExists(handle))
        return false;
    const std::initializer_list<Arg> two = {Arg::Int(handle), Arg::Float(center[0]),
        Arg::Float(center[1]), Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Int(0)};
    const std::initializer_list<Arg> three = {Arg::Int(handle), Arg::Float(center[0]), Arg::Float(center[1]),
        Arg::Float(center[2]), Arg::Float(radius[0]), Arg::Float(radius[1]), Arg::Float(radius[2]), Arg::Int(0)};
    bool success = false;
    switch (sourceOpcode) {
    case 0x0053: success = VcsScmSaCall(0x00B0, two, result); break;
    case 0x0054: success = VcsScmSaCall(0x00B1, three, result); break;
    case 0x0100: success = VcsScmSaCall(0x01AB, two, result); break;
    case 0x0101: success = VcsScmSaCall(0x01AC, three, result); break;
    case 0x0102: success = VcsScmSaCall(0x01AD, two, result); break;
    case 0x0103: success = VcsScmSaCall(0x01AE, two, result); break;
    case 0x0104: success = VcsScmSaCall(0x01AF, three, result); break;
    case 0x0105: success = VcsScmSaCall(0x01B0, three, result); break;
    default: return false;
    }
    if (!success || !marker)
        return success;
    float position[3];
    if (!VcsScmSaCall(0x00AA, {Arg::Int(handle), Arg::Out(position), Arg::Out(position + 1), Arg::Out(position + 2)}))
        return false;
    const bool threeDimensional = sourceOpcode == 0x0054 || sourceOpcode == 0x0101 ||
        sourceOpcode == 0x0104 || sourceOpcode == 0x0105;
    if (sourceOpcode == 0x0053 || sourceOpcode == 0x0054 || sourceOpcode == 0x0100 || sourceOpcode == 0x0101) {
        const float middle[] = {(center[0] + radius[0]) * 0.5f, (center[1] + radius[1]) * 0.5f,
            (center[2] + radius[2]) * 0.5f};
        const float size[] = {fabsf(radius[0] - center[0]) * 0.5f, fabsf(radius[1] - center[1]) * 0.5f,
            fabsf(radius[2] - center[2]) * 0.5f};
        return VcsScmSaDrawLocateMarker(markerId, middle, size, threeDimensional, position[2]);
    }
    return VcsScmSaDrawLocateMarker(markerId, center, radius, threeDimensional, position[2]);
}
}

void VcsScmBindSaSpatial(ReVcsScriptHost& host)
{
    host.characterLocate = CharacterLocate;
    host.characterArea = CharacterArea;
    host.characterLocateCharacter = LocateCharacter;
    host.characterLocateVehicle = LocateVehicle;
    host.characterLocateObject = LocateObject;
    host.vehicleLocate = VehicleLocate;
}
