#include "StdAfx.h"
#include "VcsScmHostSaRadar.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "VcsScmSaHud.h"
#include "VcsScmRuntime.h"
#include "ReVcsScriptVm.h"
#include "Radar.h"
#include "Frontend.h"
#include "Game.h"

namespace {
typedef VcsScmSaArg Arg;
VcsScmRadarState radarState;
bool hooksInstalled = false;

void __cdecl DrawRadarMap()
{
    VcsScmApplySaRadarTransform();
    reinterpret_cast<void (__cdecl*)()>(0x586880)();
}

void __cdecl DrawMap()
{
    reinterpret_cast<void (__cdecl*)()>(0x586B00)();
    // Interior radar skips the map tail call but still draws entity blips.
    VcsScmApplySaRadarTransform();
}

void __cdecl DrawHudRadar()
{
    if (!VcsScmReady() || VcsScmSaHudItemVisible(6))
        reinterpret_cast<void (__cdecl*)()>(0x58A330)();
}

struct FrameMarker {
    unsigned short opcode;
    unsigned int id;
    bool mission;
    float values[7];
    int colour[3];
};
FrameMarker frameMarkers[32];
unsigned int frameMarkerCount = 0;

struct SpriteMapping { unsigned char source, native; const char* texture; };
const SpriteMapping spriteMappings[] = {
    {1,2,"radar_centre"}, {3,4,"radar_north"}, {4,6,"radar_gun"},
    {6,35,"radar_save"}, {7,8,"property"}, {8,63,"radar_spray"}, {10,7,"radar_bomb"},
    {29,19,"radar_empire"}, {30,20,"radar_empire_dam"}, {31,21,"radar_empire_con"},
    {35,9,"radar_bryan"}, {36,10,"radar_gonz"}, {37,11,"radar_lance"},
    {38,43,"radar_louise"}, {39,44,"radar_marty"}, {40,46,"radar_phil"},
    {41,12,"radar_reni"}, {42,13,"radar_diaz"}, {43,47,"radar_jerry"},
    {44,14,"radar_mendez"}, {45,15,"radar_umberto"}, {46,16,"radar_plane"},
    {47,18,"radar_gunshop"}
};

int Sprite(unsigned char source)
{
    if (source == 0) return 0;
    for (unsigned int i = 0; i < sizeof(spriteMappings) / sizeof(spriteMappings[0]); ++i)
        if (spriteMappings[i].source == source)
            return CRadar::RadarBlipSprites[spriteMappings[i].native].m_pTexture ? spriteMappings[i].native : -1;
    return -1;
}

int BlipIndex(int handle)
{
    const unsigned int index = unsigned(handle) & 0xFFFF;
    if (index >= 175) return -1;
    const tRadarTrace& trace = CRadar::ms_RadarTrace[index];
    return trace.m_bTrackingBlip && trace.m_nCounter == (unsigned(handle) >> 16) ? int(index) : -1;
}

void BlipRemove(int handle)
{
    const int index = BlipIndex(handle);
    if (index >= 0) {
        VcsScmSaCall(0x0164, { Arg::Int(handle) });
        radarState.missionBlips[index] = -1;
        return;
    }
    const unsigned int slot = unsigned(handle) & 0xFFFF;
    if (slot < 175 && radarState.missionBlips[slot] == handle)
        radarState.missionBlips[slot] = -1;
}

bool BlipStyle(unsigned short source, int handle, int value)
{
    if (BlipIndex(handle) < 0) return true;
    switch (source) {
    case 0x00BC: return VcsScmSaCall(0x0165, { Arg::Int(handle), Arg::Int(value) });
    case 0x00BD: CRadar::ChangeBlipBrightness(handle, value); return true;
    case 0x00BF: return VcsScmSaCall(0x0168, { Arg::Int(handle), Arg::Int(static_cast<unsigned char>(value)) });
    case 0x00C4:
        return (unsigned(value) & 7) <= 3 && VcsScmSaCall(0x018B, { Arg::Int(handle), Arg::Int(value & 7) });
    default: return false;
    }
}

bool BlipEntityCreate(int entity, unsigned char type, int* handle)
{
    if (!handle) return false;
    bool created = false;
    switch (type) {
    case 1:
        created = VcsScmSaVehicleExists(entity) && VcsScmSaCall(0x0186, { Arg::Int(entity), Arg::Out(handle) });
        break;
    case 2:
        created = VcsScmSaPedExists(entity) && VcsScmSaCall(0x0187, { Arg::Int(entity), Arg::Out(handle) });
        break;
    case 3:
        created = VcsScmSaObjectExists(entity) && VcsScmSaCall(0x0188, { Arg::Int(entity), Arg::Out(handle) });
        break;
    default: return false;
    }
    if (!created || BlipIndex(*handle) < 0) return created;
    return VcsScmSaCall(0x0165, { Arg::Int(*handle), Arg::Int(type == 3 ? 1 : 0) }) &&
           VcsScmSaCall(0x0168, { Arg::Int(*handle), Arg::Int(3) });
}

bool BlipCreate(const float* p, unsigned char sourceSprite, unsigned char type,
                unsigned int colour, unsigned char display, bool shortRange, int* handle)
{
    const int sprite = Sprite(sourceSprite);
    if (!p || !handle || sprite < 0 || (type != 4 && type != 5) || display > 3) return false;
    bool created;
    if (type == 5) {
        if (shortRange) created = VcsScmSaCall(0x0570, {
            Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(sprite), Arg::Out(handle) });
        else created = VcsScmSaCall(0x02A7, {
            Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(sprite), Arg::Out(handle) });
    } else {
        if (shortRange) created = VcsScmSaCall(0x04CE, {
            Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(sprite), Arg::Out(handle) });
        else created = VcsScmSaCall(0x02A8, {
            Arg::Float(p[0]), Arg::Float(p[1]), Arg::Float(p[2]), Arg::Int(sprite), Arg::Out(handle) });
    }
    if (!created || BlipIndex(*handle) < 0) return created;
    return VcsScmSaCall(0x0165, { Arg::Int(*handle), Arg::Int(static_cast<int>(colour)) }) &&
           VcsScmSaCall(0x018B, { Arg::Int(*handle), Arg::Int(display) });
}

bool BlipPlainCoordCreate(const float* xyz, int* handle)
{
    return BlipCreate(xyz, 0, 4, 5, 3, false, handle) &&
        (BlipIndex(*handle) < 0 || VcsScmSaCall(0x0168, { Arg::Int(*handle), Arg::Int(3) }));
}

bool BlipPickupCreate(int pickup, int* handle)
{
    if (!handle) return false;
    bool exists = false;
    if (!VcsScmSaCall(0x09D1, { Arg::Int(pickup) }, &exists) || !exists) return false;
    if (!VcsScmSaCall(0x03DC, { Arg::Int(pickup), Arg::Out(handle) })) return false;
    return BlipIndex(*handle) < 0 || VcsScmSaCall(0x0168, { Arg::Int(*handle), Arg::Int(3) });
}

bool BlipClearAll()
{
    for (int i = 0; i < 175; ++i) {
        const tRadarTrace& trace = CRadar::ms_RadarTrace[i];
        const int handle = int((unsigned(trace.m_nCounter) << 16) | unsigned(i));
        if (trace.m_bTrackingBlip && handle != FrontEndMenuManager.m_nTargetBlipIndex) BlipRemove(handle);
    }
    return VcsScmSaError() == NULL;
}

void BlipMissionOwner(int handle, bool mission)
{
    const int index = BlipIndex(handle);
    if (index >= 0) radarState.missionBlips[index] = mission ? handle : -1;
}

bool BlipGround(float x, float y, float* z)
{
    return z && VcsScmSaCall(0x02CE, { Arg::Float(x), Arg::Float(y), Arg::Float(1000.0f), Arg::Out(z) });
}

bool RadarRange(float range)
{
    if (!(range >= -100000.0f && range <= 100000.0f) || (range > 0 && range < 0.0001f)) return false;
    radarState.range = range;
    return true;
}

bool RadarCoordinateOrigin(const float* xyz)
{
    if (!xyz) return false;
    for (int i = 0; i < 3; ++i) if (!(xyz[i] >= -100000.0f && xyz[i] <= 100000.0f)) return false;
    memcpy(radarState.origin, xyz, sizeof(radarState.origin));
    radarState.originActive = xyz[0] != 0 || xyz[1] != 0 || xyz[2] != 0;
    radarState.originArea = CGame::currArea;
    return true;
}

void RadarDisplayMode(int mode)
{
    VcsScmSaCall(0x0581, { Arg::Int(mode != 2) });
}

bool MarkerCreate(unsigned int scheduler, unsigned int outputIp, const float* xyz,
                  float radius, unsigned char type, bool mission, int* handle)
{
    if (!xyz || !handle || (type != 6 && type != 7) || !(radius > 0 && radius <= 1000)) return false;
    for (int i = 0; i < 3; ++i) if (!(xyz[i] > -100000 && xyz[i] < 100000)) return false;
    *handle = -1;
    for (unsigned int i = 0; i < 16; ++i) {
        VcsScmRetainedMarker& marker = radarState.markers[i];
        if (marker.active) continue;
        marker.generation = marker.generation % 65534 + 1;
        marker.id = 0x40000000u | ((scheduler * 16777619u ^ outputIp ^ i) & 0x3FFFFFFFu);
        marker.active = 1;
        marker.type = type;
        marker.mission = mission;
        memcpy(marker.position, xyz, sizeof(marker.position));
        marker.radius = radius;
        *handle = int((marker.generation << 16) | i);
        break;
    }
    return true;
}

void MarkerRemove(int handle)
{
    const unsigned int slot = unsigned(handle) & 0xFFFF;
    if (slot < 16 && radarState.markers[slot].generation == (unsigned(handle) >> 16))
        radarState.markers[slot].active = 0;
}

bool FrameMarkerRequest(unsigned short opcode, unsigned int id, const float* p, const int* rgb)
{
    if (!p || !rgb || (opcode != 0x0235 && opcode != 0x03D2 && opcode != 0x03E2 && opcode != 0x04BF)) return false;
    const bool directional = opcode == 0x03E2 || opcode == 0x04BF;
    for (int i = 0; i < (directional ? 7 : 4); ++i)
        if (!(p[i] >= -100000 && p[i] <= 100000)) return false;
    const float radius = opcode == 0x03E2 ? 3.2f : opcode == 0x04BF ? p[6] : p[3];
    if (!(radius > 0 && radius <= 1000)) return false;
    unsigned int index = 0;
    while (index < frameMarkerCount && frameMarkers[index].id != id) ++index;
    if (index == 32) return true; // Native frame renderer also drops on pool exhaustion.
    FrameMarker& marker = frameMarkers[index];
    marker.opcode = opcode;
    marker.id = id;
    marker.mission = false;
    memcpy(marker.values, p, sizeof(marker.values));
    memcpy(marker.colour, rgb, sizeof(marker.colour));
    if (!directional && (p[2] == -100.0f || p[2] <= -250.0f) &&
        !BlipGround(p[0], p[1], &marker.values[2])) return false;
    if (index == frameMarkerCount) ++frameMarkerCount;
    return true;
}

void FrameMarkerMissionOwner(unsigned short, unsigned int id, const float*, bool mission)
{
    for (unsigned int i = 0; i < frameMarkerCount; ++i)
        if (frameMarkers[i].id == id) frameMarkers[i].mission = mission;
}

void Place(unsigned int id, unsigned short type, CVector position, float size, const int* rgb,
           unsigned char alpha, unsigned short period, float pulse, short rotation, CVector direction)
{
    reinterpret_cast<void* (__cdecl*)(unsigned int, unsigned short, CVector&, float,
        unsigned char, unsigned char, unsigned char, unsigned char, unsigned short,
        float, short, CVector, bool)>(0x725120)(id, type, position, size,
        static_cast<unsigned char>(rgb[0]), static_cast<unsigned char>(rgb[1]),
        static_cast<unsigned char>(rgb[2]), alpha, period, pulse, rotation, direction, false);
}

void RenderFrameMarker(const FrameMarker& marker)
{
    const float* p = marker.values;
    if (marker.opcode == 0) {
        reinterpret_cast<void (__cdecl*)(unsigned int, float, float, float, float, float)>(0x485E00)(
            marker.id, p[0] - p[3], p[1] - p[4], p[0] + p[3], p[1] + p[4], p[2]);
        return;
    }
    CVector position(p[0], p[1], p[2]);
    CVector direction(0.0f, 0.0f, 1.0f);
    if (marker.opcode == 0x0235) {
        Place(marker.id, 1, position, p[3], marker.colour, 228, 2048, 0.1f, 1, direction);
        Place(marker.id, 1, position, p[3] * 0.93f, marker.colour, 228, 2048, 0.1f, -1, direction);
    } else if (marker.opcode == 0x03D2) {
        Place(marker.id, 1, position, p[3] * 0.7f, marker.colour, 255, 128, 0.0f, 1, direction);
    } else {
        direction = CVector(p[3] - p[0], p[4] - p[1], marker.opcode == 0x03E2 ? 0.0f : p[5] - p[2]);
        const float length = sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (length > 0.0001f) direction = direction * (1.0f / length);
        else direction = CVector(1.0f, 0.0f, 0.0f);
        if (marker.opcode == 0x03E2) {
            position.z += 7.0f + p[6];
            reinterpret_cast<void (__cdecl*)(CVector, float, int, int, int, int, float, float, float)>(0x721140)(
                position, 3.2f, marker.colour[0], marker.colour[1], marker.colour[2], 200,
                direction.x, direction.y, direction.z);
        } else Place(marker.id, 4, position, p[6], marker.colour, 100, 1, 0.0f, 0, direction);
    }
}

void MissionFinished()
{
    // CMissionCleanup owns release of native entities and engine mission state.
    reinterpret_cast<void (__thiscall*)(void*)>(0x468560)(reinterpret_cast<void*>(0xA90850));
    for (int i = 0; i < 175; ++i)
        if (radarState.missionBlips[i] != -1) BlipRemove(radarState.missionBlips[i]);
    for (int i = 0; i < 16; ++i)
        if (radarState.markers[i].mission) radarState.markers[i].active = 0;
    unsigned int remaining = 0;
    for (unsigned int i = 0; i < frameMarkerCount; ++i)
        if (!frameMarkers[i].mission) frameMarkers[remaining++] = frameMarkers[i];
    frameMarkerCount = remaining;
}

} // namespace

void VcsScmLoadSaRadarSprites(CPNGArchive& archive)
{
    if (!VcsScmEnabled()) return;
    for (unsigned int i = 0; i < sizeof(spriteMappings) / sizeof(spriteMappings[0]); ++i) {
        CSprite2d& sprite = CRadar::RadarBlipSprites[spriteMappings[i].native];
        if (!sprite.m_pTexture) sprite.SetTextureFromSPTA(archive, spriteMappings[i].texture);
    }
}

bool VcsScmInstallSaRadarHooks()
{
    if (hooksInstalled) return true;
    const unsigned char mapTail[] = {0xE9, 0x2D, 0xFB, 0xFF, 0xFF};
    const unsigned char mapCall[] = {0xE8, 0x71, 0xC7, 0xFF, 0xFF};
    const unsigned char hudCall[] = {0xE8, 0xD8, 0xA6, 0xFF, 0xFF};
    if (memcmp(reinterpret_cast<const void*>(0x586D4E), mapTail, sizeof(mapTail)) ||
        memcmp(reinterpret_cast<const void*>(0x58A38A), mapCall, sizeof(mapCall)) ||
        memcmp(reinterpret_cast<const void*>(0x58FC53), hudCall, sizeof(hudCall))) return false;
    Memory::InjectHook(0x586D4E, &DrawRadarMap, PATCH_JUMP);
    Memory::InjectHook(0x58A38A, &DrawMap, PATCH_CALL);
    Memory::InjectHook(0x58FC53, &DrawHudRadar, PATCH_CALL);
    hooksInstalled = true;
    return true;
}

void VcsScmApplySaRadarTransform()
{
    if (!VcsScmReady()) return;
    if (radarState.range > 0) CRadar::m_radarRange = radarState.range;
    if (radarState.originActive && radarState.originArea == CGame::currArea)
        CRadar::vec2DRadarOrigin = CVector2D(radarState.origin[0], radarState.origin[1]);
}

void VcsScmSaMarkersBeginPass() { frameMarkerCount = 0; }

bool VcsScmSaLocateMarker(unsigned int id, const float* center, const float* radius, bool three)
{
    if (!center || !radius) return false;
    for (int i = 0; i < 3; ++i)
        if (!(center[i] >= -10000 && center[i] <= 10000)) return false;
    for (int i = 0; i < (three ? 3 : 2); ++i)
        if (!(radius[i] >= -10000 && radius[i] <= 10000)) return false;
    unsigned int index = 0;
    while (index < frameMarkerCount && frameMarkers[index].id != id) ++index;
    if (index == 32) return true;
    FrameMarker& marker = frameMarkers[index];
    memset(&marker, 0, sizeof(marker));
    marker.id = id;
    marker.mission = VcsScmIsPlayerOnMission();
    memcpy(marker.values, center, 3 * sizeof(float));
    memcpy(marker.values + 3, radius, (three ? 3 : 2) * sizeof(float));
    if (index == frameMarkerCount) ++frameMarkerCount;
    return true;
}

void VcsScmSaMarkersRender()
{
    if (!VcsScmReady()) return;
    for (unsigned int i = 0; i < frameMarkerCount; ++i) RenderFrameMarker(frameMarkers[i]);
    for (int i = 0; i < 16; ++i) {
        const VcsScmRetainedMarker& marker = radarState.markers[i];
        if (!marker.active) continue;
        const bool arrow = marker.type == 7;
        CVector position(marker.position[0], marker.position[1], marker.position[2]);
        if (arrow) position.z += 1.5f * marker.radius;
        const int colour[] = {255, arrow ? 255 : 224, arrow ? 255 : 64};
        Place(marker.id, arrow ? 0 : 1, position, marker.radius * (arrow ? 2.0f : 1.0f),
            colour, arrow ? 255 : 192, 1024, arrow ? 0.2f : 0.25f, arrow ? 5 : 0, CVector(0,0,1));
    }
}

void VcsScmResetSaRadar()
{
    memset(&radarState, 0, sizeof(radarState));
    for (int i = 0; i < 175; ++i) radarState.missionBlips[i] = -1;
    frameMarkerCount = 0;
}

void VcsScmCaptureSaRadar(VcsScmRadarState& state) { state = radarState; }

bool VcsScmValidateSaRadar(const VcsScmRadarState& state)
{
    if (state.originActive > 1 || !(state.range >= -100000 && state.range <= 100000) ||
        (state.range > 0 && state.range < 0.0001f)) return false;
    for (int i = 0; i < 3; ++i) if (!(state.origin[i] >= -100000 && state.origin[i] <= 100000)) return false;
    for (int i = 0; i < 175; ++i)
        if (state.missionBlips[i] != -1 && (unsigned(state.missionBlips[i]) & 0xFFFF) != unsigned(i)) return false;
    for (int i = 0; i < 16; ++i) {
        const VcsScmRetainedMarker& marker = state.markers[i];
        if (marker.active > 1 || marker.mission > 1 || marker.generation > 65534) return false;
        if (!marker.active) continue;
        if (!marker.generation || (marker.id & 0xC0000000u) != 0x40000000u ||
            (marker.type != 6 && marker.type != 7) ||
            !(marker.radius > 0 && marker.radius <= 1000)) return false;
        for (int j = 0; j < 3; ++j)
            if (!(marker.position[j] > -100000 && marker.position[j] < 100000)) return false;
    }
    return true;
}

bool VcsScmRestoreSaRadar(const VcsScmRadarState& state)
{
    if (!VcsScmValidateSaRadar(state)) return false;
    radarState = state;
    frameMarkerCount = 0;
    return true;
}

void VcsScmBindSaRadar(ReVcsScriptHost& host)
{
    host.blipRemove = BlipRemove;
    host.blipStyle = BlipStyle;
    host.blipEntityCreate = BlipEntityCreate;
    host.blipCreate = BlipCreate;
    host.blipPlainCoordCreate = BlipPlainCoordCreate;
    host.blipPickupCreate = BlipPickupCreate;
    host.blipClearAll = BlipClearAll;
    host.blipMissionOwner = BlipMissionOwner;
    host.blipGround = BlipGround;
    host.radarRange = RadarRange;
    host.radarCoordinateOrigin = RadarCoordinateOrigin;
    host.radarDisplayMode = RadarDisplayMode;
    host.markerCreate = MarkerCreate;
    host.markerRemove = MarkerRemove;
    host.frameMarker = FrameMarkerRequest;
    host.frameMarkerMissionOwner = FrameMarkerMissionOwner;
    host.missionFinished = MissionFinished;
}
