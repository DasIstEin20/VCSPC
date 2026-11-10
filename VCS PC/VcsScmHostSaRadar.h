#pragma once

struct ReVcsScriptHost;
class CPNGArchive;

struct VcsScmRetainedMarker {
    unsigned int active;
    unsigned int generation;
    unsigned int id;
    unsigned int type;
    unsigned int mission;
    float position[3];
    float radius;
};

struct VcsScmRadarState {
    VcsScmRetainedMarker markers[16];
    int missionBlips[175];
    float range;
    float origin[3];
    int originArea;
    unsigned int originActive;
};
static_assert(sizeof(VcsScmRadarState) == 1300, "Radar adapter save layout changed");

void VcsScmBindSaRadar(ReVcsScriptHost& host);
bool VcsScmInstallSaRadarHooks();
void VcsScmLoadSaRadarSprites(CPNGArchive& archive);
void VcsScmApplySaRadarTransform();
void VcsScmSaMarkersBeginPass();
void VcsScmSaMarkersRender();
bool VcsScmSaLocateMarker(unsigned int id, const float* center, const float* radius, bool three);
void VcsScmResetSaRadar();
void VcsScmCaptureSaRadar(VcsScmRadarState& state);
bool VcsScmValidateSaRadar(const VcsScmRadarState& state);
bool VcsScmRestoreSaRadar(const VcsScmRadarState& state);
