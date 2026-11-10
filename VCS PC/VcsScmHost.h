#pragma once

#include <stdio.h>
#include "VcsScmSaHud.h"
#include "VcsScmHostSaGarages.h"
#include "VcsScmSaStatistics.h"
#include "VcsScmHostSaRadar.h"
#include "VcsScmHostSaPopulation.h"

struct ReVcsScriptHost;

// Called after the native IDE/model tables exist and before the VM starts.
// A manifest row is a VCS source ID and a native model name separated by whitespace.
bool VcsScmLoadModelMap(const char* path, FILE* log);
void VcsScmBuildHost(ReVcsScriptHost& host);
void VcsScmResetHost();

// Persist only adapter-owned state. The native save owns the live world.
struct VcsScmHostState {
    int weatherOld;
    int weatherNew;
    float weatherInterpolation;
    float weatherRain;
    unsigned int weatherStored;
    unsigned int pauseDisabled;
    VcsScmHudState hud;
    VcsScmGarageState garages;
    VcsScmStatisticsState statistics;
    VcsScmRadarState radar;
    VcsScmPopulationState population;
};
static_assert(sizeof(VcsScmHostState) == 24 + sizeof(VcsScmHudState) + sizeof(VcsScmGarageState) +
    sizeof(VcsScmStatisticsState) + sizeof(VcsScmRadarState) + sizeof(VcsScmPopulationState),
    "SCM host save layout changed");

void VcsScmCaptureHostState(VcsScmHostState& state);
bool VcsScmValidateHostState(const VcsScmHostState& state);
bool VcsScmRestoreHostState(const VcsScmHostState& state);

// VCS player operands are generation-checked ped handles, not SA player slots.
int VcsScmPlayerSlot(int pedHandle);
int VcsScmModelBySource(int source);
int VcsScmSourceByModel(int model);
