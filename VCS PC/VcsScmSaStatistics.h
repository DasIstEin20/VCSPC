#pragma once

struct ReVcsScriptHost;

// VCS statistics without an existing SA slot belong to the native CStats owner.
struct VcsScmStatisticsState {
    int activity[19];
    unsigned int empireRecords[7];
    unsigned int empireJobs[6];
    int empireEarnings;
    int exportTotal;
    unsigned int empireEnabled;
    unsigned char empireOwned[4];
    unsigned char empireBusinesses[8];
    unsigned int stages[3];
    float maximumJumpDistance, maximumJumpHeight;
    int drivingRangeScore;
    float drivingRangeDistance;
    int shootingTime, shootingAccuracy;
    int waterExtra[2][2];
    float caddyAirDistance;
};
static_assert(sizeof(VcsScmStatisticsState) == 208, "SCM statistics save layout");

void VcsScmBindSaStatistics(ReVcsScriptHost& host);
void VcsScmResetStatisticsState();
void VcsScmCaptureStatisticsState(VcsScmStatisticsState& state);
bool VcsScmValidateStatisticsState(const VcsScmStatisticsState& state);
bool VcsScmRestoreStatisticsState(const VcsScmStatisticsState& state);
bool VcsScmStatisticsEnabled();
