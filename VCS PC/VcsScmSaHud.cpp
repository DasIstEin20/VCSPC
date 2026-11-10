#include "StdAfx.h"
#include "VcsScmSaHud.h"
#include "VcsScmSaCommand.h"
#include "VcsScmRuntime.h"
#include "ReVcsScriptVm.h"
#include "UserDisplay.h"
#include "Text.h"
#include "Timer.h"
#include "Camera.h"
#include "Audio.h"
#include "Frontend.h"

// Source policy is reVCS df799eb6 OnscreenTimer and NativeScriptDisplay.
// The native owners retain stable mirrors, never a temporary SA script local.
namespace {
enum { ClockDown = 1, ClockFlashing = 2, ClockShown = 4 };
enum { CounterNumber = 1, CounterMoney = 2, CounterFlashing = 4, CounterShown = 8 };
const unsigned int SourceCounterCount = 3;
VcsScmHudState hud = {};
int clockValue = 0;
int counterValues[4] = {};
bool clockNeedsWrite = false;

unsigned int PackedColour(const CRGBA& colour)
{
    unsigned int packed;
    memcpy(&packed, &colour, sizeof(packed));
    return packed;
}

CRGBA Colour(unsigned int packed)
{
    CRGBA colour;
    memcpy(&colour, &packed, sizeof(packed));
    return colour;
}

bool ValidKey(const char* key)
{
    if (!key) return true;
    const size_t length = strlen(key);
    return length <= 8 && (!length || TheText.Get(key));
}

void CopyKey(char (&destination)[8], const char* key)
{
    memset(destination, 0, sizeof(destination));
    if (key) memcpy(destination, key, strlen(key));
}

unsigned int ConsumeColour(unsigned int channel, unsigned int current, unsigned int fallback)
{
    const unsigned int next = hud.pendingColours[channel];
    if (next == 0xFFFFFFFFu) return fallback;
    hud.pendingColours[channel] = 0xFFFFFFFFu;
    // Original VCS consumes RGB and keeps the slot owner's alpha.
    return (current & 0xFF000000u) | (next & 0x00FFFFFFu);
}

void RebindClock()
{
    COnscreenTimerEntry& entry = *CUserDisplay::OnscnTimer.GetTimer();
    memset(&entry, 0, sizeof(entry));
    if (!hud.clock.active) return;
    entry.m_pVariable = &clockValue;
    memcpy(entry.m_cGXTentry, hud.clock.key, 8);
    entry.m_bCountsDown = (hud.clock.flags & ClockDown) != 0;
    entry.m_bFlashing = (hud.clock.flags & ClockFlashing) != 0;
    entry.m_bShown = (hud.clock.flags & ClockShown) != 0;
    entry.m_nSoundOnTime = hud.clock.soundOnTime;
    entry.m_colour = Colour(hud.clock.colour | 0xFF000000u);
    entry.m_shadowColour = Colour(hud.clock.shadowColour);
}

void RebindCounter(unsigned int slot)
{
    COnscreenCounterEntry& entry = CUserDisplay::OnscnTimer.GetCounter()[slot];
    memset(&entry, 0, sizeof(entry));
    entry.m_wOwnsWhat = 0xFFFF;
    const VcsScmHudCounterState& saved = hud.counters[slot];
    if (!saved.active) return;
    entry.m_pVariable = &counterValues[slot];
    memcpy(entry.m_cGXTentry, saved.key, 8);
    entry.m_wType = saved.type == 0 ? 0 : 1;
    entry.m_bShown = (saved.flags & CounterShown) != 0;
    entry.m_bFlashing = (saved.flags & CounterFlashing) != 0;
    entry.m_bShowsDollar = (saved.flags & CounterMoney) != 0;
    entry.m_colour = Colour(saved.colour | 0xFF000000u);
    entry.m_shadowColour = CRGBA(0, 0, 0, 255);
}

void CounterColour(bool background, const unsigned char* rgba)
{
    if (!rgba) { VcsScmSaReject("SCM HUD colour has no RGBA value"); return; }
    memcpy(&hud.pendingColours[background ? 1 : 0], rgba, 4);
}

bool HudItemVisible(unsigned char item, bool visible)
{
    if (item >= 13) return VcsScmSaReject("SCM HUD item is outside 0..12");
    const unsigned int mask = 1u << item;
    if (visible) hud.hiddenItems &= ~mask;
    else hud.hiddenItems |= mask;
    return true;
}

void DisplayWidescreenMoney(bool visible) { hud.widescreenMoney = visible; }

bool OnscreenCounter(unsigned int offset, unsigned short type, const char* key,
                     bool show, unsigned short position)
{
    int value;
    if (position >= SourceCounterCount || !ValidKey(key))
        return VcsScmSaReject("SCM HUD counter has an invalid row or text key");
    if (!VcsScmReadGlobal(offset, &value)) return false;
    if (!show) {
        // VCS clears every duplicate registration of the same VM global.
        for (unsigned int slot = 0; slot < SourceCounterCount; ++slot) {
            if (hud.counters[slot].active && hud.counters[slot].offset == offset) {
                hud.counters[slot].active = 0;
                RebindCounter(slot);
            }
        }
        return true;
    }
    VcsScmHudCounterState& saved = hud.counters[position];
    if (saved.active) return true;
    const unsigned int oldColour = saved.colour;
    const unsigned int oldBackground = saved.backgroundColour;
    memset(&saved, 0, sizeof(saved));
    saved.active = 1;
    saved.offset = offset;
    saved.type = type;
    saved.flags = CounterFlashing;
    saved.maximum = -1;
    saved.colour = ConsumeColour(0, oldColour, 0x9D000000u);
    saved.backgroundColour = ConsumeColour(1, oldBackground, 0x51000000u);
    CopyKey(saved.key, key);
    counterValues[position] = value;
    RebindCounter(position);
    return true;
}

bool OnscreenCounterNumber(unsigned int offset, int maximum, unsigned short type, const char* key)
{
    const bool added = !hud.counters[0].active;
    if (!OnscreenCounter(offset, type, key, true, 0)) return false;
    if (added) {
        hud.counters[0].flags |= CounterNumber;
        hud.counters[0].maximum = maximum;
    }
    VcsScmSaHudFormatCounter(CUserDisplay::OnscnTimer.GetCounter());
    return true;
}

bool OnscreenCounterDual(unsigned int offset, int maximum, unsigned short type,
                         unsigned short auxiliary, const char* heading, const char* key)
{
    if (!ValidKey(heading)) return VcsScmSaReject("SCM HUD heading is not a valid text key");
    const bool added = !hud.counters[0].active;
    if (!OnscreenCounterNumber(offset, maximum, type, key)) return false;
    if (added) {
        CopyKey(hud.counters[0].heading, heading);
        hud.counters[0].auxiliary = auxiliary;
    }
    return true;
}

bool OnscreenCounterMoney(unsigned int offset, bool enabled)
{
    int value;
    if (!VcsScmReadGlobal(offset, &value)) return false;
    for (unsigned int slot = 0; slot < SourceCounterCount; ++slot) {
        VcsScmHudCounterState& saved = hud.counters[slot];
        if (saved.active && saved.offset == offset) {
            if (enabled) saved.flags |= CounterMoney;
            else saved.flags &= ~CounterMoney;
            CUserDisplay::OnscnTimer.GetCounter()[slot].m_bShowsDollar = enabled;
            counterValues[slot] = value;
            VcsScmSaHudFormatCounter(CUserDisplay::OnscnTimer.GetCounter() + slot);
            break;
        }
    }
    return true;
}

bool OnscreenTimer(unsigned short opcode, unsigned int offset, bool value, const char* key)
{
    if (opcode == 0x022D) {
        hud.paused = value;
        CUserDisplay::OnscnTimer.m_bPaused = value;
        return true;
    }
    int current;
    if (!ValidKey(key)) return VcsScmSaReject("SCM HUD clock has an invalid text key");
    if (!VcsScmReadGlobal(offset, &current)) return false;
    if (opcode == 0x00AC) {
        if (hud.clock.active && hud.clock.offset == offset) {
            hud.clock.active = 0;
            RebindClock();
        }
        return true;
    }
    if (opcode != 0x00AB && opcode != 0x024C)
        return VcsScmSaReject("SCM HUD clock received an unsupported operation");
    if (hud.clock.active) return true;
    hud.clock.active = 1;
    hud.clock.offset = offset;
    hud.clock.flags = value ? ClockDown : 0;
    hud.clock.soundOnTime = 12;
    hud.clock.colour = ConsumeColour(0, hud.clock.colour, 0xFF5BE1F4u);
    hud.clock.shadowColour = 0xFF000000u;
    CopyKey(hud.clock.key, key);
    clockValue = current;
    RebindClock();
    return true;
}

bool SavedKeyValid(const char (&key)[8])
{
    bool ended = false;
    for (unsigned int i = 0; i < 8; ++i) {
        const unsigned char value = static_cast<unsigned char>(key[i]);
        if (!value) ended = true;
        else if (ended || value < 32 || value > 126) return false;
    }
    return true;
}

bool ReadValues(const VcsScmHudState& state, int* timer, int* counters)
{
    if (state.clock.active && !VcsScmReadGlobal(state.clock.offset, timer)) return false;
    for (unsigned int slot = 0; slot < 4; ++slot)
        if (state.counters[slot].active && !VcsScmReadGlobal(state.counters[slot].offset, counters + slot))
            return false;
    return true;
}
}

void VcsScmResetHudState()
{
    COnscreenTimerEntry* clock = CUserDisplay::OnscnTimer.GetTimer();
    if (clock->m_pVariable == &clockValue) CUserDisplay::OnscnTimer.ClearClock(&clockValue);
    for (unsigned int slot = 0; slot < 4; ++slot)
        CUserDisplay::OnscnTimer.ClearCounter(counterValues + slot);
    memset(&hud, 0, sizeof(hud));
    hud.pendingColours[0] = hud.pendingColours[1] = 0xFFFFFFFFu;
    hud.clock.colour = 0xFF5BE1F4u;
    for (unsigned int slot = 0; slot < 4; ++slot) {
        hud.counters[slot].colour = 0x9D000000u;
        hud.counters[slot].backgroundColour = 0x51000000u;
        hud.counters[slot].maximum = -1;
    }
    clockNeedsWrite = false;
    CUserDisplay::OnscnTimer.m_bPaused = false;
}

void VcsScmCaptureHudState(VcsScmHudState& state)
{
    hud.paused = CUserDisplay::OnscnTimer.m_bPaused;
    const COnscreenTimerEntry& clock = *CUserDisplay::OnscnTimer.GetTimer();
    if (hud.clock.active && clock.m_pVariable != &clockValue) hud.clock.active = 0;
    if (hud.clock.active) {
        hud.clock.flags = (clock.m_bCountsDown ? ClockDown : 0) |
            (clock.m_bFlashing ? ClockFlashing : 0) | (clock.m_bShown ? ClockShown : 0);
        hud.clock.soundOnTime = clock.m_nSoundOnTime;
        hud.clock.shadowColour = PackedColour(clock.m_shadowColour);
    }
    for (unsigned int slot = 0; slot < 4; ++slot) {
        const COnscreenCounterEntry& entry = CUserDisplay::OnscnTimer.GetCounter()[slot];
        VcsScmHudCounterState& saved = hud.counters[slot];
        if (saved.active && entry.m_pVariable != counterValues + slot) saved.active = 0;
        saved.flags = (saved.flags & (CounterNumber | CounterMoney)) |
            (entry.m_bFlashing ? CounterFlashing : 0) | (entry.m_bShown ? CounterShown : 0);
    }
    state = hud;
}

bool VcsScmValidateHudState(const VcsScmHudState& state)
{
    const VcsScmHudClockState& clock = state.clock;
    if (state.paused > 1 || (state.hiddenItems & ~0x1FFFu) || state.widescreenMoney > 1 ||
        state.helpDuringWidescreen > 1 || clock.active > 1 || (clock.flags & ~7u) ||
        !SavedKeyValid(clock.key) || (clock.active && (!clock.offset || (clock.offset & 3)))) return false;
    for (unsigned int slot = 0; slot < 4; ++slot) {
        const VcsScmHudCounterState& counter = state.counters[slot];
        if (counter.active > 1 || counter.type > 0xFFFF || counter.auxiliary > 0xFFFF ||
            (counter.flags & ~15u) || !SavedKeyValid(counter.key) || !SavedKeyValid(counter.heading) ||
            (counter.active && (slot >= SourceCounterCount || !counter.offset || (counter.offset & 3))))
            return false;
    }
    return true;
}

bool VcsScmValidateHudGlobals(const VcsScmHudState& state)
{
    int timer = 0, counters[4] = {};
    return VcsScmValidateHudState(state) && ReadValues(state, &timer, counters);
}

bool VcsScmRestoreHudState(const VcsScmHudState& state)
{
    int timer = 0, counters[4] = {};
    if (!VcsScmValidateHudState(state) || !ReadValues(state, &timer, counters)) return false;
    VcsScmResetHudState();
    hud = state;
    clockValue = timer;
    memcpy(counterValues, counters, sizeof(counters));
    RebindClock();
    for (unsigned int slot = 0; slot < 4; ++slot) RebindCounter(slot);
    CUserDisplay::OnscnTimer.m_bPaused = hud.paused != 0;
    CUserDisplay::OnscnTimer.ProcessForDisplay();
    return true;
}

bool VcsScmSaHudBeginDisplay()
{
    VcsScmHudState snapshot;
    VcsScmCaptureHudState(snapshot);
    clockNeedsWrite = hud.clock.active != 0;
    if (!ReadValues(hud, &clockValue, counterValues)) {
        clockNeedsWrite = false;
        return false;
    }
    return true;
}

bool VcsScmSaHudEndDisplay()
{
    const bool writeClock = clockNeedsWrite;
    clockNeedsWrite = false;
    if (writeClock && !VcsScmWriteGlobal(hud.clock.offset, clockValue)) return false;
    if (hud.clock.active && CUserDisplay::OnscnTimer.GetTimer()->m_pVariable != &clockValue)
        hud.clock.active = 0;
    return true;
}

bool VcsScmSaHudProcessClock(COnscreenTimerEntry* entry)
{
    if (!hud.clock.active || entry->m_pVariable != &clockValue) return false;
    const int previous = clockValue;
    const int step = static_cast<int>(CTimer::ms_fTimeStep * 20.0f);
    const unsigned int next = entry->m_bCountsDown ?
        static_cast<unsigned int>(previous) - static_cast<unsigned int>(step) :
        static_cast<unsigned int>(previous) + static_cast<unsigned int>(step);
    memcpy(&clockValue, &next, sizeof(clockValue));
    if (entry->m_bCountsDown) {
        if (clockValue < 0) {
            clockValue = 0;
            entry->m_pVariable = nullptr;
            entry->m_bShown = false;
            entry->m_cGXTentry[0] = 0;
        } else if (previous / 1000 < 12 && clockValue / 1000 != previous / 1000 && !TheCamera.m_WideScreenOn) {
            AudioEngine.ReportFrontendAudioEvent(33, 0.0f, 1.0f);
        }
    }
    return true;
}

bool VcsScmSaHudFormatClock(COnscreenTimerEntry* entry)
{
    if (!hud.clock.active || entry->m_pVariable != &clockValue) return false;
    const unsigned int time = static_cast<unsigned int>(clockValue);
    sprintf(entry->m_cDisplayedText, "%02u:%02u", time / 1000 / 60 % 100, time / 1000 % 60);
    return true;
}

bool VcsScmSaHudFormatCounter(COnscreenCounterEntry* entry)
{
    const ptrdiff_t slot = entry - CUserDisplay::OnscnTimer.GetCounter();
    if (slot < 0 || slot >= 4 || !hud.counters[slot].active || entry->m_pVariable != counterValues + slot)
        return false;
    VcsScmHudCounterState& saved = hud.counters[slot];
    const bool money = (saved.flags & CounterMoney) != 0;
    const bool suffix = FrontEndMenuManager.GetLanguage() == 4; // SA LANGUAGE_SPANISH
    const char* before = money && !suffix ? "$" : "";
    const char* after = money && suffix ? "$" : "";
    char formatted[32];
    const int value = counterValues[slot];
    if ((saved.flags & CounterNumber) && saved.maximum != -1) {
        if (static_cast<unsigned int>(saved.maximum) >= 99) saved.maximum = 99;
        sprintf(formatted, "%s%d%s/%d", before, value < 99 ? value : 99, after, saved.maximum);
    } else sprintf(formatted, "%s%d%s", before, value, after);
    if (saved.type) entry->m_counterData.nBarFill = atoi(formatted);
    else strcpy(entry->m_counterData.cDisplayedText, formatted);
    return true;
}

const char* VcsScmSaHudCounterHeading(unsigned int slot)
{
    return slot < 4 && hud.counters[slot].active && hud.counters[slot].heading[0] ?
        hud.counters[slot].heading : nullptr;
}

CRGBA VcsScmSaHudCounterBackground(unsigned int slot, const CRGBA& fallback)
{
    return slot < 4 && hud.counters[slot].active ?
        Colour(hud.counters[slot].backgroundColour | 0xFF000000u) : fallback;
}

const char* VcsScmSaHudText(const char* key)
{
    char terminated[9] = {};
    memcpy(terminated, key, 8);
    return TheText.Get(terminated);
}

bool VcsScmSaHudItemVisible(unsigned int item)
{
    return item < 13 && *bWants_To_Draw_Hud && !(hud.hiddenItems & (1u << item));
}

bool VcsScmSaHudWidescreenMoney()
{
    return hud.widescreenMoney && !(hud.hiddenItems & (1u << 2));
}

bool VcsScmSaHudHelpDuringWidescreen() { return hud.helpDuringWidescreen != 0; }
void VcsScmSaHudSetHelpDuringWidescreen(bool enabled) { hud.helpDuringWidescreen = enabled; }

void VcsScmBindSaHud(ReVcsScriptHost& host)
{
    VcsScmResetHudState();
    host.counterColour = CounterColour;
    host.onscreenCounter = OnscreenCounter;
    host.onscreenCounterNumber = OnscreenCounterNumber;
    host.onscreenCounterDual = OnscreenCounterDual;
    host.onscreenCounterMoney = OnscreenCounterMoney;
    host.onscreenTimer = OnscreenTimer;
    host.hudItemVisible = HudItemVisible;
    host.displayWidescreenMoney = DisplayWidescreenMoney;
}
