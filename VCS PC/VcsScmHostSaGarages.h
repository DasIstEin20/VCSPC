#pragma once

struct ReVcsScriptHost;

struct VcsScmGarageState {
    unsigned char capacity[50];
    unsigned char locked[50];
};
static_assert(sizeof(VcsScmGarageState) == 100, "Garage adapter save layout changed");

bool VcsScmInstallSaGarageHooks();
void VcsScmBindSaGarages(ReVcsScriptHost& host);
void VcsScmResetSaGarages();
void VcsScmCaptureSaGarages(VcsScmGarageState& state);
bool VcsScmValidateSaGarages(const VcsScmGarageState& state);
bool VcsScmRestoreSaGarages(const VcsScmGarageState& state);
