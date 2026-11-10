#pragma once

struct ReVcsScriptHost;
class COnscreenTimerEntry;
class COnscreenCounterEntry;
class CRGBA;

// No native pointers cross the save boundary. VM globals use source byte offsets.
struct VcsScmHudClockState {
    unsigned int active, offset, flags;
    int soundOnTime;
    char key[8];
    unsigned int colour, shadowColour;
};

struct VcsScmHudCounterState {
    unsigned int active, offset, type, flags;
    int maximum;
    unsigned int auxiliary;
    char key[8], heading[8];
    unsigned int colour, backgroundColour;
};

struct VcsScmHudState {
    VcsScmHudClockState clock;
    VcsScmHudCounterState counters[4];
    unsigned int pendingColours[2];
    unsigned int paused;
    unsigned int hiddenItems, widescreenMoney, helpDuringWidescreen;
};
static_assert(sizeof(VcsScmHudClockState) == 32, "SCM HUD clock save layout");
static_assert(sizeof(VcsScmHudCounterState) == 48, "SCM HUD counter save layout");
static_assert(sizeof(VcsScmHudState) == 248, "SCM HUD save layout");

void VcsScmBindSaHud(ReVcsScriptHost& host);
void VcsScmResetHudState();
void VcsScmCaptureHudState(VcsScmHudState& state);
bool VcsScmValidateHudState(const VcsScmHudState& state);
bool VcsScmValidateHudGlobals(const VcsScmHudState& state);
bool VcsScmRestoreHudState(const VcsScmHudState& state);

bool VcsScmSaHudBeginDisplay();
bool VcsScmSaHudEndDisplay();
bool VcsScmSaHudProcessClock(COnscreenTimerEntry* entry);
bool VcsScmSaHudFormatClock(COnscreenTimerEntry* entry);
bool VcsScmSaHudFormatCounter(COnscreenCounterEntry* entry);
const char* VcsScmSaHudCounterHeading(unsigned int slot);
CRGBA VcsScmSaHudCounterBackground(unsigned int slot, const CRGBA& fallback);
const char* VcsScmSaHudText(const char* key);
bool VcsScmSaHudItemVisible(unsigned int item);
bool VcsScmSaHudWidescreenMoney();
bool VcsScmSaHudHelpDuringWidescreen();
void VcsScmSaHudSetHelpDuringWidescreen(bool enabled);
