#include "StdAfx.h"
#include "VcsScmHostInternal.h"
#include "ReVcsScriptVm.h"
#include "Camera.h"
#include "World.h"
#include "Pools.h"

// SA 1.0 US entry points, checked against gta-reversed afbc26d70675b9138.
namespace {
void Fade(float seconds, short direction)
{
    reinterpret_cast<void (__thiscall*)(CCamera*, float, short)>(0x50AC20)(&TheCamera, seconds, direction);
}

void FadeColour(unsigned char red, unsigned char green, unsigned char blue)
{
    reinterpret_cast<void (__thiscall*)(CCamera*, unsigned char, unsigned char, unsigned char)>(0x50BF00)(
        &TheCamera, red, green, blue);
}

bool Fading()
{
    return TheCamera.GetFading();
}

void Widescreen(bool enabled)
{
    if (enabled)
        reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50C140)(&TheCamera);
    else
        reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50C150)(&TheCamera);
}

void DisplayHud(bool enabled)
{
    *bWants_To_Draw_Hud = enabled;
}

bool HudDrawRequested()
{
    return *bWants_To_Draw_Hud != 0;
}

void NearClip(float value)
{
    reinterpret_cast<void (__thiscall*)(CCamera*, float)>(0x50BF90)(&TheCamera, value);
}

void FixedCamera(const float* values)
{
    const CVector position(values[0], values[1], values[2]);
    const CVector up(values[3], values[4], values[5]);
    reinterpret_cast<void (__thiscall*)(CCamera*, const CVector&, const CVector&)>(0x50BEC0)(
        &TheCamera, position, up);
}

void PointCamera(const float* xyz, short switchType, bool ground)
{
    CVector point(xyz[0], xyz[1], xyz[2]);
    if (ground) {
        bool found = false;
        point.z = static_cast<float>(CWorld::FindGroundZFor3DCoord(point.x, point.y, 1000.0f, &found, nullptr));
    }
    reinterpret_cast<void (__thiscall*)(CCamera*, const CVector&, short, int)>(0x50C8B0)(
        &TheCamera, point, switchType, 1);
}

bool PointCharacter(int handle, short sourceMode, short switchType)
{
    CPed* ped = handle >= 0 && !(handle & 0x80) && CPools::GetPedPool() ?
        CPools::GetPedPool()->GetAt(handle) : nullptr;
    if (!ped)
        return true;
    if (sourceMode != 15)
        return false;
    reinterpret_cast<void (__thiscall*)(CCamera*, CEntity*, short, short, int)>(0x50C7C0)(
        &TheCamera, ped, MODE_FIXED, switchType, 1);
    return true;
}

bool PointEntity(unsigned short opcode, int handle, short sourceMode, short switchType)
{
    CEntity* entity = nullptr;
    if (opcode == 0x00B4) {
        entity = handle >= 0 && !(handle & 0x80) && CPools::GetVehiclePool() ?
            CPools::GetVehiclePool()->GetAt(handle) : nullptr;
        if (!entity)
            return true;
    } else if (opcode != 0x00B3)
        return false;

    short nativeMode = MODE_FIXED;
    if (sourceMode == 3 && opcode == 0x00B4)
        nativeMode = MODE_BEHINDCAR;
    else if (sourceMode != 15)
        return false;
    reinterpret_cast<void (__thiscall*)(CCamera*, CEntity*, short, short, int)>(0x50C7C0)(
        &TheCamera, entity, nativeMode, switchType, 1);
    return true;
}

void RestoreCamera(bool jumpCut)
{
    if (jumpCut)
        reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50BAB0)(&TheCamera);
    else
        reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50B930)(&TheCamera);
}

void CameraBehindPlayer()
{
    reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50BD40)(&TheCamera);
}

void CameraInFrontOfPlayer()
{
    reinterpret_cast<void (__thiscall*)(CCamera*)>(0x50BD70)(&TheCamera);
}

void CameraInterpolation(float percentage, int milliseconds)
{
    reinterpret_cast<void (__thiscall*)(CCamera*, float, float, unsigned int)>(0x50C030)(
        &TheCamera, percentage, 100.0f - percentage, static_cast<unsigned int>(milliseconds));
}

bool CameraCoordinates(unsigned short opcode, float* xyz)
{
    if (!xyz || (opcode != 0x02AD && opcode != 0x02B5 && opcode != 0x0470))
        return false;
    CVector position = opcode == 0x0470 ? TheCamera.GetCoords() : TheCamera.Cams[2].Source;
    if (opcode == 0x02B5)
        position += TheCamera.Cams[2].Front;
    xyz[0] = position.x;
    xyz[1] = position.y;
    xyz[2] = position.z;
    return true;
}

bool CameraFirstPerson(int, bool* result)
{
    if (!result)
        return false;
    *result = reinterpret_cast<bool (__thiscall*)(CCamera*)>(0x50BFF0)(&TheCamera);
    return true;
}

void PedZoom(int indicator)
{
    if (TheCamera.Cams[TheCamera.ActiveCam].Mode == MODE_FOLLOWPED)
        TheCamera.m_nPedZoom = indicator;
}

bool PedZoomQuery(int* indicator)
{
    if (!indicator)
        return false;
    *indicator = TheCamera.Cams[TheCamera.ActiveCam].Mode == MODE_FOLLOWPED ? TheCamera.m_nPedZoom : -1;
    return true;
}

bool VehicleZoomQuery(int* indicator)
{
    if (!indicator)
        return false;
    *indicator = TheCamera.Cams[TheCamera.ActiveCam].Mode == MODE_CAM_ON_A_STRING ? TheCamera.m_nCarZoom : -1;
    return true;
}

void VehicleZoom(int indicator)
{
    if (TheCamera.Cams[TheCamera.ActiveCam].Mode == MODE_CAM_ON_A_STRING)
        TheCamera.m_nCarZoom = indicator;
}

void CameraShake(float strength)
{
    CamShakeNoPos(&TheCamera, strength);
}
}

void VcsScmBindCamera(ReVcsScriptHost& host)
{
    host.fade = Fade;
    host.fadeColour = FadeColour;
    host.fading = Fading;
    host.widescreen = Widescreen;
    host.displayHud = DisplayHud;
    host.hudDrawRequested = HudDrawRequested;
    host.nearClip = NearClip;
    host.cameraFixed = FixedCamera;
    host.cameraPoint = PointCamera;
    host.cameraPointCharacter = PointCharacter;
    host.cameraPointEntity = PointEntity;
    host.cameraRestore = RestoreCamera;
    host.cameraBehindPlayer = CameraBehindPlayer;
    host.cameraInFrontOfPlayer = CameraInFrontOfPlayer;
    host.cameraInterpolation = CameraInterpolation;
    host.cameraCoordinates = CameraCoordinates;
    host.cameraFirstPerson = CameraFirstPerson;
    host.cameraPedZoom = PedZoom;
    host.cameraPedZoomQuery = PedZoomQuery;
    host.cameraVehicleZoomQuery = VehicleZoomQuery;
    host.vehicleCameraZoomBase = VehicleZoom;
    host.cameraShake = CameraShake;
}
