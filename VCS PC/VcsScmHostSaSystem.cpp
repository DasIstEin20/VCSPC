#include "StdAfx.h"
#include "VcsScmSaPresentation.h"
#include "VcsScmSaCommand.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "ReVcsScriptVm.h"
#include "Pad.h"
#include "World.h"
#include "Pools.h"
#include "Audio.h"

// SA command contracts from SannyBuilder 4ea6da3c and gta-reversed afbc26d7.
// Semantic input selectors follow reVCS NativeScriptInput at df799eb6.
namespace {
using Arg = VcsScmSaArg;
bool pauseDisabled = false;
short queryAxes[4] = {};

void PauseDisabled(bool disabled) { pauseDisabled = disabled; }
void PauseInWidescreen(bool allowed) { VcsScmSaCall(0x0A48, {Arg::Int(allowed)}); }

bool CutsceneLoad(const char* name)
{
    return name && *name && strlen(name) <= 8 && VcsScmSaCall(0x02E4, {Arg::Text(name)});
}

bool CutsceneLoaded()
{
    bool loaded = false;
    return VcsScmSaCall(0x06B9, {}, &loaded) && loaded;
}

bool CutsceneStart() { return VcsScmSaCall(0x02E7, {}); }
void CutsceneClear() { VcsScmSaCall(0x02EA, {}); }

bool CutsceneFinished()
{
    bool finished = false;
    return VcsScmSaCall(0x02E9, {}, &finished) && finished;
}

bool MusicFade(bool enabled) { return VcsScmSaCall(0x043C, {Arg::Int(enabled)}); }
bool ActivateSaveMenu() { return VcsScmSaCall(0x03D8, {}); }

bool SaveMenuFinished(bool* finished)
{
    return finished && VcsScmSaCall(0x03D9, {}, finished);
}

bool ButtonPressed(unsigned short pad, unsigned short button, bool* pressed)
{
    if (pad >= 2 || button >= 20 || !pressed)
        return false;
    int state = 0;
    // GET_PAD_STATE retains the source raw-state behavior. SA's separate
    // IS_BUTTON_PRESSED command also filters JustOutOfFrontend.
    if (!VcsScmSaCall(0x00E2, {Arg::Int(pad), Arg::Int(button), Arg::Out(&state)}))
        return false;
    *pressed = state != 0;
    return true;
}

bool AnalogueSticks(int pad, int* axes)
{
    return pad >= 0 && pad < 2 && axes && VcsScmSaCall(0x0494,
        {Arg::Int(pad), Arg::Out(axes), Arg::Out(axes + 1), Arg::Out(axes + 2), Arg::Out(axes + 3)});
}

bool PadState(unsigned int selector, int* state)
{
    CPad* pad = CPad::GetPad(0);
    if (!pad || !state)
        return false;
    CPed* player = CWorld::Players[CWorld::PlayerInFocus].pPed;
    const bool disabled = pad->DisablePlayerControls != 0;
    int value;
    switch (selector) {
    case 0: value = pad->Mode < 3 ? pad->NewState.RIGHTSHOCK != 0 :
                   pad->Mode == 3 && pad->NewState.CROSS != 0; break;
    case 1: case 17: value = pad->GetAccelerate(); break;
    case 2: value = pad->GetSteeringLeftRight(); break;
    case 3: value = pad->GetSteeringUpDown(); break;
    case 4: value = pad->GetCarGunLeftRight(); break;
    case 5: value = pad->GetCarGunUpDown(); break;
    case 6: value = pad->GetPedWalkLeftRight(); break;
    case 7: value = pad->GetPedWalkUpDown(); break;
    case 8: value = pad->GetLookLeft(); break;
    case 9: value = pad->GetLookRight(); break;
    case 10: value = pad->GetLookBehindForCar(); break;
    case 11: value = pad->GetLookBehindForPed(); break;
    case 12: value = pad->GetHorn(); break;
    case 13: value = pad->HornJustDown(); break;
    case 14: value = pad->GetExitVehicle(); break;
    case 15: value = pad->ExitVehicleJustDown(); break;
    case 16: value = pad->GetHandBrake(); break;
    case 18:
        if (!player) return false;
        value = pad->GetWeapon(player); break;
    case 19:
        if (!player) return false;
        value = pad->WeaponJustDown(player); break;
    case 20: value = disabled ? 0 : pad->NewState.TRIANGLE; break;
    case 21: value = !disabled && pad->NewState.TRIANGLE && !pad->OldState.TRIANGLE; break;
    case 22: value = disabled ? 0 : pad->NewState.SQUARE; break;
    case 23: value = pad->CycleCameraModeJustDown(); break;
    case 24: value = pad->CycleWeaponLeftJustDown(); break;
    case 25: value = pad->CycleWeaponRightJustDown(); break;
    case 26: value = pad->GetTarget(); break;
    case 27: value = pad->TargetJustDown(); break;
    case 28: value = !disabled && pad->NewState.CROSS && !pad->OldState.CROSS; break;
    case 29:
        value = !disabled && pad->Mode < 4 && pad->NewState.SQUARE &&
            abs(static_cast<int>(pad->GetPedWalkLeftRight())) +
            abs(static_cast<int>(pad->GetPedWalkUpDown())) < 65;
        break;
    case 30:
        value = !disabled && pad->NewState.RIGHTSHOULDER1 &&
            pad->NewState.LEFTSHOULDER2 && !pad->OldState.LEFTSHOULDER2;
        break;
    case 31: {
        if (!player) return false;
        int weapon = 0;
        if (!VcsScmSaCall(0x0470,
            {Arg::Int(CPools::GetPedPool()->GetHandle(player)), Arg::Out(&weapon)}))
            return false;
        value = weapon != VcsScmSaWeaponType(16) && weapon != VcsScmSaWeaponType(17) &&
            !disabled && !pad->GetLookBehindForPed() && pad->NewState.LEFTSHOULDER1;
        break;
    }
    case 32: value = pad->SniperZoomIn(); break;
    case 33: value = pad->SniperZoomOut(); break;
    case 34: value = pad->LookAroundLeftRight(); break;
    case 35: value = pad->LookAroundUpDown(); break;
    case 36:
        if (!player) return false;
        value = pad->SniperModeLookLeftRight(player); break;
    case 37:
        if (!player) return false;
        value = pad->SniperModeLookUpDown(player); break;
    case 38: case 39: case 40: case 41: {
        const unsigned int direction = selector - 38;
        const short axis = direction < 2 ? pad->GetAnalogueLeftRight() : pad->GetAnalogueUpDown();
        const bool positive = direction == 1 || direction == 3;
        const bool edge = positive ?
            axis > (direction == 1 ? 25 : 15) && queryAxes[direction] < (direction == 1 ? 21 : 6) :
            axis < -15 && queryAxes[direction] >= -5;
        queryAxes[direction] = axis;
        const short current[] = {pad->NewState.DPADLEFT, pad->NewState.DPADRIGHT,
                                 pad->NewState.DPADUP, pad->NewState.DPADDOWN};
        const short previous[] = {pad->OldState.DPADLEFT, pad->OldState.DPADRIGHT,
                                  pad->OldState.DPADUP, pad->OldState.DPADDOWN};
        value = edge || (current[direction] && !previous[direction]);
        break;
    }
    case 42: value = pad->NewState.SQUARE; break;
    case 43: value = pad->NewState.CIRCLE; break;
    case 44: value = pad->NewState.SQUARE && !pad->OldState.SQUARE; break;
    default: return false;
    }
    *state = value;
    return true;
}

void ScriptApplyBrakes(int, bool enabled)
{
    // VCS collects a controller operand but the engine owner is always pad0.
    CPad::GetPad(0)->bApplyBrakes = enabled;
}

bool PlayerDrunkInputDelay(int handle, int delay)
{
    return VcsScmPlayerSlot(handle) >= 0 && delay >= 0 && delay < 10 &&
        VcsScmSaCall(0x03FD, {Arg::Int(0), Arg::Int(delay)});
}

void WorldProcessing(bool enabled) { VcsScmSaCall(0x03B7, {Arg::Int(enabled)}); }

bool OneOffSound(const float*, int source)
{
    // reVCS handles these full-word frontend IDs before narrowing spatial
    // sounds. Values here are named SA eAudioEvents from gta-reversed afbc26d7.
    int event;
    switch (source) {
    case 1: event = 43; break; // AE_FRONTEND_PART_MISSION_COMPLETE
    case 7: case 8: case 9: event = 44; break; // AE_FRONTEND_RACE_321
    case 10: event = 45; break; // AE_FRONTEND_RACE_GO
    case 13: event = 40; break; // AE_FRONTEND_PURCHASE_WEAPON
    case 14: event = 14; break; // AE_FRONTEND_CAR_NO_CASH
    default: {
        char message[112];
        sprintf(message, "SCM one-off sound %d needs a VCS-to-SA audio event/asset mapping", source);
        return VcsScmSaReject(message);
    }
    }
    AudioEngine.ReportFrontendAudioEvent(event, 0.0f, 1.0f);
    return true;
}

}

bool VcsScmSaPauseDisabled() { return pauseDisabled; }
void VcsScmSaSetPauseDisabled(bool disabled) { pauseDisabled = disabled; }
void VcsScmSaResetInputQueries() { memset(queryAxes, 0, sizeof(queryAxes)); }

void VcsScmBindSaSystem(ReVcsScriptHost& host)
{
    pauseDisabled = false;
    VcsScmSaResetInputQueries();
    host.pauseDisabled = PauseDisabled;
    host.pauseInWidescreen = PauseInWidescreen;
    host.cutsceneLoad = CutsceneLoad;
    host.cutsceneLoaded = CutsceneLoaded;
    host.cutsceneStart = CutsceneStart;
    host.cutsceneFinished = CutsceneFinished;
    host.cutsceneClear = CutsceneClear;
    host.musicFade = MusicFade;
    host.activateSaveMenu = ActivateSaveMenu;
    host.saveMenuFinished = SaveMenuFinished;
    host.buttonPressed = ButtonPressed;
    host.analogueSticks = AnalogueSticks;
    host.padState = PadState;
    host.scriptApplyBrakes = ScriptApplyBrakes;
    host.playerDrunkInputDelay = PlayerDrunkInputDelay;
    host.worldProcessing = WorldProcessing;
    host.oneOffSound = OneOffSound;
}
