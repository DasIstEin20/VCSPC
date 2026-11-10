#pragma once

struct ReVcsScriptHost;

struct VcsScmPopulationState {
    int pedModels[9][2];
    int vehicleModels[9];
};
static_assert(sizeof(VcsScmPopulationState) == 108, "Population adapter save layout changed");

bool VcsScmInstallSaPopulationHooks();
void VcsScmBindSaPopulation(ReVcsScriptHost& host);
void VcsScmResetSaPopulation();
void VcsScmCaptureSaPopulation(VcsScmPopulationState& state);
bool VcsScmValidateSaPopulation(const VcsScmPopulationState& state);
bool VcsScmValidateSaPopulationModels(const VcsScmPopulationState& state);
bool VcsScmRestoreSaPopulation(const VcsScmPopulationState& state);
