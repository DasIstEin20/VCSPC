#include "StdAfx.h"
#include "VcsScmSaStatistics.h"
#include "VcsScmSaCommand.h"
#include "ReVcsScriptVm.h"
#include "Stats.h"

#include <float.h>
#include <limits.h>

// Opcode operands and record rules follow reVCS df799eb6 NativeScriptStatistics
// and CStats. Native VCSPC statistic identifiers are deliberately named here.
namespace {
using Arg = VcsScmSaArg;
bool enabled = false;

void AddWord(int& value, int delta)
{
    const unsigned int sum = static_cast<unsigned int>(value) + static_cast<unsigned int>(delta);
    memcpy(&value, &sum, sizeof(value));
}

void Maximum(int& value, int candidate) { if (candidate > value) value = candidate; }
void Maximum(float& value, float candidate) { if (candidate > value) value = candidate; }
bool Finite(float value) { return value >= -FLT_MAX && value <= FLT_MAX; }

int FloatToNativeStat(float value)
{
    if (value >= 2147483648.0f) return INT_MAX;
    if (value <= -2147483648.0f) return INT_MIN;
    return static_cast<int>(value);
}

void MissionGiven() { VcsScmSaCall(0x0317, {}); }
void OddjobPassed() { VcsScmSaCall(0x0595, {}); }

bool MissionPassed(const char* key)
{
    return key && *key && strlen(key) <= 8 && VcsScmSaCall(0x0318, {Arg::Text(key)});
}

void ExportedCarsAdd(int delta) { AddWord(PlayerStatsInteger[VEHICLES_EXPORTED], delta); }
void ExportCarsTotal(int total) { CStats::ScmStatistics.exportTotal = total; }
void EmpireEnabled(bool value) { CStats::ScmStatistics.empireEnabled = value; }
void EmpireEarnings(int delta) { AddWord(CStats::ScmStatistics.empireEarnings, delta); }

void EmpireOwned(const unsigned char* counts)
{
    if (!counts) { VcsScmSaReject("SCM empire ownership has no count array"); return; }
    memcpy(CStats::ScmStatistics.empireOwned, counts, 4);
}

bool EmpireBusiness(int type, unsigned char count)
{
    if (type < 0 || type > 6) return VcsScmSaReject("SCM empire business type is outside 0..6");
    CStats::ScmStatistics.empireBusinesses[type] = count;
    return true;
}

void GameProgress(int selector, bool value)
{
    if (selector >= 0 && selector < 3) {
        CStats::ScmStatistics.stages[selector] = value;
        PlayerStatsInteger[CITIES_PASSED] = CStats::ScmStatistics.stages[0] +
            CStats::ScmStatistics.stages[1] + CStats::ScmStatistics.stages[2];
    } else if (selector == 3) {
        if (value) PlayerStatsFloat[PROGRESS_MADE] = PlayerStatsFloat[TOTAL_PROGRESS];
    } else VcsScmSaReject("SCM game progress selector is outside 0..3");
}

void JumpTaxiStat(unsigned short opcode, int raw)
{
    float value;
    memcpy(&value, &raw, sizeof(value));
    switch (opcode) {
    case 0x01E1:
        if (!Finite(value)) { VcsScmSaReject("SCM jump distance is not finite"); return; }
        Maximum(CStats::ScmStatistics.maximumJumpDistance, value);
        PlayerStatsInteger[MAX_JUMP_DISTANCE] = FloatToNativeStat(CStats::ScmStatistics.maximumJumpDistance);
        break;
    case 0x01E2:
        if (!Finite(value)) { VcsScmSaReject("SCM jump height is not finite"); return; }
        Maximum(CStats::ScmStatistics.maximumJumpHeight, value);
        PlayerStatsInteger[MAX_JUMP_HEIGHT] = FloatToNativeStat(CStats::ScmStatistics.maximumJumpHeight);
        break;
    case 0x01E3: Maximum(PlayerStatsInteger[MAX_JUMP_FLIPS], raw); break;
    case 0x01E4: Maximum(PlayerStatsInteger[MAX_JUMP_SPINS], raw); break;
    case 0x01E5: Maximum(PlayerStatsInteger[BEST_STUNT], raw); break;
    case 0x01E6: AddWord(PlayerStatsInteger[UNIQUE_JUMPS_DONE], 1); break;
    case 0x01E8: AddWord(PlayerStatsInteger[PASSENGERS_DELIVERED_IN_TAXI], 1); break;
    case 0x01E9: AddWord(PlayerStatsInteger[TAXI_MONEY_MADE], raw); break;
    default: VcsScmSaReject("SCM jump/taxi statistic operation is unsupported"); break;
    }
}

bool RaceRecord(unsigned short opcode, int course, int value)
{
    unsigned int count = 0, base = 0, stride = 2, member = 0;
    bool position = false, water = false;
    switch (opcode) {
    case 0x0522: count = 9; base = TURISMO1_BEST_POSITION; stride = 3; position = true; break;
    case 0x0521: count = 9; base = TURISMO1_BEST_LAP; stride = 3; break;
    case 0x0520: count = 9; base = TURISMO1_BEST_TIME; stride = 3; break;
    case 0x051F: count = 12; base = DTRIAL1_BEST_LAP; break;
    case 0x051E: count = 12; base = DTRIAL1_BEST_TIME; break;
    case 0x0538: count = 8; base = BTRIAL1_BEST_LAP; break;
    case 0x0537: count = 8; base = BTRIAL1_BEST_TIME; break;
    case 0x053B: count = 4; base = QTTRIAL1_BEST_LAP; break;
    case 0x053A: count = 4; base = QTTRIAL1_BEST_TIME; break;
    case 0x051D: count = 8; base = MTRIAL1_BEST_LAP; water = true; member = 1; break;
    case 0x051C: count = 8; base = MTRIAL1_BEST_TIME; water = true; break;
    default: return VcsScmSaReject("SCM race record operation is unsupported");
    }
    if (course < 0 || static_cast<unsigned int>(course) >= count)
        return VcsScmSaReject("SCM race course is outside the source table");
    int& current = water && course >= 6 ? CStats::ScmStatistics.waterExtra[course - 6][member] :
        PlayerStatsInteger[base + course * stride];
    if ((!position && current == 0) || value < current) current = value;
    return true;
}

bool DrivingRangeRecord(int score, float distance)
{
    if (!Finite(distance)) return VcsScmSaReject("SCM driving range distance is not finite");
    Maximum(CStats::ScmStatistics.drivingRangeScore, score);
    Maximum(CStats::ScmStatistics.drivingRangeDistance, distance);
    return true;
}

void ActivityRecord(unsigned int record, int value)
{
    int& current = CStats::ScmStatistics.activity[record];
    if (record == 0 || record == 12 || record >= 16) Maximum(current, value);
    else if ((record <= 2 && current == 0) || value < current) current = value;
    // These five activities already have named VCSPC frontend entries.
    switch (record) {
    case 4: PlayerStatsInteger[BEST_TIME_PCJ1] = current; break;
    case 5: PlayerStatsInteger[BEST_TIME_PCJ2] = current; break;
    case 9: PlayerStatsInteger[BEST_TIME_PCJ3] = current; break;
    case 10: PlayerStatsInteger[BEST_TIME_PCJ4] = current; break;
    case 12: PlayerStatsInteger[BEST_SCORE_CRASH] = current; break;
    }
}

bool TrialDistance(unsigned short opcode, int raw)
{
    float distance;
    memcpy(&distance, &raw, sizeof(distance));
    if (!Finite(distance)) return VcsScmSaReject("SCM trial air distance is not finite");
    const float quantized = static_cast<float>(FloatToNativeStat(distance * 10.0f)) * 0.1f;
    if (opcode == 0x0561) {
        Maximum(CStats::ScmStatistics.caddyAirDistance, quantized);
        return true;
    }
    unsigned int stat;
    switch (opcode) {
    case 0x0535: stat = FURTHEST_HOOP_DTRIAL; break;
    case 0x0536: stat = FURTHEST_HOOP_BTRIAL; break;
    case 0x0539: stat = FURTHEST_HOOP_QTTRIAL; break;
    default: return VcsScmSaReject("SCM trial air statistic operation is unsupported");
    }
    Maximum(PlayerStatsFloat[stat], quantized);
    return true;
}

bool GameStatistic(unsigned short opcode, int value)
{
    VcsScmStatisticsState& stats = CStats::ScmStatistics;
    switch (opcode) {
    case 0x027A: AddWord(PlayerStatsInteger[LIVES_SAVED], 1); return true;
    case 0x027B: AddWord(PlayerStatsInteger[CRIMINALS_CAUGHT], 1); return true;
    case 0x027C: Maximum(PlayerStatsInteger[AMBULANCE_LEVEL], value); return true;
    case 0x027D: AddWord(PlayerStatsInteger[FIRES_EXTINGUISHED], 1); return true;
    case 0x032E: PlayerStatsFloat[WEAPON_BUDGET] += static_cast<float>(value); return true;
    case 0x0351: Maximum(PlayerStatsInteger[VIGILANTE_LEVEL], value); return true;
    case 0x036E: Maximum(PlayerStatsInteger[FIREFIGHTER_LEVEL], value); return true;
    case 0x03DF: {
        float distance;
        memcpy(&distance, &value, sizeof(distance));
        if (!Finite(distance)) return VcsScmSaReject("SCM face plant distance is not finite");
        Maximum(PlayerStatsFloat[LONGEST_FACE_PLANT], distance);
        return true;
    }
    case 0x0535: case 0x0536: case 0x0539: case 0x0561: return TrialDistance(opcode, value);
    case 0x053C: if (value < stats.shootingTime) stats.shootingTime = value; return true;
    case 0x053D: Maximum(stats.shootingAccuracy, value); return true;
    case 0x053E: Maximum(PlayerStatsInteger[SHOOTING_RANGE_SCORE], value); return true;
    case 0x0546:
        if (value < 1 || value > 6) return VcsScmSaReject("SCM empire job type is outside 1..6");
        ++stats.empireJobs[value - 1]; return true;
    case 0x055B: Maximum(PlayerStatsInteger[BEST_SCORE_RUSH], value); return true;
    case 0x050A: case 0x050B: case 0x050C: case 0x050D:
        ++stats.empireRecords[opcode - 0x050A]; return true;
    case 0x0550: case 0x0551:
        stats.empireRecords[4 + opcode - 0x0550] += static_cast<unsigned int>(value); return true;
    case 0x0552:
        if (static_cast<unsigned int>(value) > stats.empireRecords[6])
            stats.empireRecords[6] = static_cast<unsigned int>(value);
        return true;
    case 0x0518: ActivityRecord(0, value); return true;
    case 0x0560: ActivityRecord(1, value); return true;
    case 0x0562: ActivityRecord(2, value); return true;
    case 0x0519: case 0x051A: case 0x051B: ActivityRecord(3 + opcode - 0x0519, value); return true;
    case 0x052B: case 0x052C: ActivityRecord(6 + opcode - 0x052B, value); return true;
    case 0x055C: case 0x055D: case 0x055E: case 0x055F: ActivityRecord(8 + opcode - 0x055C, value); return true;
    case 0x050E: case 0x050F: case 0x0510: case 0x0511: ActivityRecord(12 + opcode - 0x050E, value); return true;
    case 0x0514: ActivityRecord(16, value); return true;
    case 0x0515: ActivityRecord(17, value); return true;
    case 0x0517: ActivityRecord(18, value); return true;
    default: return VcsScmSaReject("SCM statistic operation is unsupported");
    }
}
}

bool VcsScmStatisticsEnabled() { return enabled; }

void VcsScmResetStatisticsState()
{
    enabled = false;
    memset(&CStats::ScmStatistics, 0, sizeof(CStats::ScmStatistics));
    for (unsigned int index = 3; index <= 11; ++index) CStats::ScmStatistics.activity[index] = INT_MAX;
    for (unsigned int index = 13; index <= 15; ++index) CStats::ScmStatistics.activity[index] = INT_MAX;
    CStats::ScmStatistics.shootingTime = INT_MAX;
}

void VcsScmCaptureStatisticsState(VcsScmStatisticsState& state) { state = CStats::ScmStatistics; }

bool VcsScmValidateStatisticsState(const VcsScmStatisticsState& state)
{
    return state.empireEnabled <= 1 && state.empireBusinesses[7] == 0 &&
        state.stages[0] <= 1 && state.stages[1] <= 1 && state.stages[2] <= 1 &&
        Finite(state.maximumJumpDistance) && state.maximumJumpDistance >= 0 &&
        Finite(state.maximumJumpHeight) && state.maximumJumpHeight >= 0 &&
        Finite(state.drivingRangeDistance) && state.drivingRangeDistance >= 0 &&
        Finite(state.caddyAirDistance) && state.caddyAirDistance >= 0;
}

bool VcsScmRestoreStatisticsState(const VcsScmStatisticsState& state)
{
    if (!VcsScmValidateStatisticsState(state)) return false;
    CStats::ScmStatistics = state;
    enabled = true;
    return true;
}

void VcsScmBindSaStatistics(ReVcsScriptHost& host)
{
    VcsScmResetStatisticsState();
    enabled = true;
    for (unsigned int course = 0; course < 9; ++course)
        PlayerStatsInteger[TURISMO1_BEST_POSITION + course * 3] = INT_MAX;
    host.gameStatistic = GameStatistic;
    host.raceRecord = RaceRecord;
    host.drivingRangeRecord = DrivingRangeRecord;
    host.missionGiven = MissionGiven;
    host.missionPassed = MissionPassed;
    host.oddjobPassed = OddjobPassed;
    host.jumpTaxiStat = JumpTaxiStat;
    host.exportedCarsAdd = ExportedCarsAdd;
    host.exportCarsTotal = ExportCarsTotal;
    host.empireStatsEnabled = EmpireEnabled;
    host.empireEarningsAdd = EmpireEarnings;
    host.empireOwnedCounts = EmpireOwned;
    host.empireBusinessCount = EmpireBusiness;
    host.gameProgress = GameProgress;
}
