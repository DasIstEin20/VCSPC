#include "StdAfx.h"
#include "VcsScmHost.h"
#include "VcsScmHostInternal.h"
#include "VcsScmSaPresentation.h"
#include "ReVcsScriptVm.h"
#include "Clock.h"
#include "Weather.h"
#include "Stats.h"
#include "World.h"
#include "Timer.h"

// Native entry points are qualified for the SA 1.0 US ABI already used by
// VCSPC. Contracts were checked against gta-reversed afbc26d70675b9138.
namespace {
// Source weather zero maps to SA SUNNY_SF. VCS restores this initial snapshot
// even before STORE, and retains the live forced weather selector.
VcsScmHostState savedWeather = {5, 5, 0.0f, 0.0f, 0, 0, {}, {}, {}, {}, {}};

float& WeatherRain()
{
    return *reinterpret_cast<float*>(0xC81324);
}

void ClockGet(unsigned char* hours, unsigned char* minutes)
{
    *hours = CClock::GetHours();
    *minutes = CClock::GetMinutes();
}

void ClockSet(unsigned char hours, unsigned char minutes)
{
    // VCS wraps the fields separately. SA would otherwise carry minutes.
    reinterpret_cast<void (__cdecl*)(unsigned char, unsigned char, unsigned char)>(0x52D150)(
        hours % 24, minutes % 60, 0);
}

int ClockMinutesUntil(unsigned char hours, unsigned char minutes)
{
    return reinterpret_cast<unsigned short (__cdecl*)(unsigned char, unsigned char)>(0x52CEB0)(hours, minutes);
}

void ClockStoreRestore(bool restore)
{
    if (restore)
        reinterpret_cast<void (__cdecl*)()>(0x52D070)();
    else
        reinterpret_cast<void (__cdecl*)()>(0x52D020)();
}

bool TimeScale(float scale)
{
    if (!(scale >= -20000000.0f && scale <= 20000000.0f))
        return false;
    CTimer::ms_fTimeScale = scale;
    return true;
}

bool ForceWeather(short source)
{
    if (source < 0 || source >= 8)
        return false;
    CWeather::ForceWeatherNow(static_cast<short>(CWeather::weatherMap[source]));
    return true;
}

bool ForceWeatherGradual(short source)
{
    if (source < 0 || source >= 8)
        return false;
    CWeather::ForcedWeatherType = static_cast<short>(CWeather::weatherMap[source]);
    return true;
}

bool ReleaseWeather()
{
    CWeather::ReleaseWeather();
    return true;
}

bool StoreWeather(bool restore)
{
    if (restore) {
        CWeather::OldWeatherType = static_cast<short>(savedWeather.weatherOld);
        CWeather::NewWeatherType = static_cast<short>(savedWeather.weatherNew);
        CWeather::InterpolationValue = savedWeather.weatherInterpolation;
        WeatherRain() = savedWeather.weatherRain;
    } else {
        savedWeather.weatherOld = CWeather::OldWeatherType;
        savedWeather.weatherNew = CWeather::NewWeatherType;
        savedWeather.weatherInterpolation = CWeather::InterpolationValue;
        savedWeather.weatherRain = WeatherRain();
        savedWeather.weatherStored = 1;
    }
    return true;
}

void UniqueTotal(int total)
{
    PlayerStatsInteger[TOTAL_UNIQUE_JUMPS] = total;
}

void UniqueFound(int total)
{
    PlayerStatsInteger[UNIQUE_JUMPS_DONE] = total;
}

void ProgressTotal(int total)
{
    PlayerStatsFloat[TOTAL_PROGRESS] = static_cast<float>(total);
}

void ProgressMade(int delta)
{
    float& made = PlayerStatsFloat[PROGRESS_MADE];
    const float total = PlayerStatsFloat[TOTAL_PROGRESS];
    made += static_cast<float>(delta);
    if (made == total - 1.0f)
        made += 1.0f;
    if (made > total)
        made = total;
}

float ProgressPercentage()
{
    const float total = PlayerStatsFloat[TOTAL_PROGRESS];
    return total == 0.0f ? 0.0f : Min(100.0f * PlayerStatsFloat[PROGRESS_MADE] / total, 100.0f);
}

int DaysPassed()
{
    return PlayerStatsInteger[DAYS_PASSED];
}

void KillFrenzyTotal(int total)
{
    PlayerStatsInteger[TOTAL_KILL_FRENZIES] = total;
}

unsigned int RandomU31()
{
    // Preserve reVCS's three-draw adapter using VCSPC's CRT random stream.
    const unsigned int low = static_cast<unsigned int>(rand()) & 0x7fff;
    const unsigned int high = static_cast<unsigned int>(rand()) & 0x7fff;
    const unsigned int top = static_cast<unsigned int>(rand()) & 1;
    return low | (high << 15) | (top << 30);
}

bool SceneLoad(const float* xyz)
{
    if (!xyz)
        return false;
    const CVector point(xyz[0], xyz[1], xyz[2]);
    reinterpret_cast<void (__cdecl*)(const CVector&)>(0x40EB70)(point);
    reinterpret_cast<void (__cdecl*)(const CVector&)>(0x40ED80)(point);
    return true;
}

float GroundZ3D(float x, float y, float z)
{
    bool found = false;
    return static_cast<float>(CWorld::FindGroundZFor3DCoord(x, y, z, &found, nullptr));
}

bool PickupGround(float x, float y, float* z)
{
    if (!z)
        return false;
    bool found = false;
    *z = static_cast<float>(CWorld::FindGroundZFor3DCoord(x, y, 1000.0f, &found, nullptr));
    return found;
}

}

void VcsScmResetEnvironment()
{
    savedWeather = {CWeather::weatherMap[0], CWeather::weatherMap[0], 0.0f, 0.0f, 0, 0, {}, {}, {}, {}, {}};
}

void VcsScmCaptureHostState(VcsScmHostState& state)
{
    state = savedWeather;
    state.pauseDisabled = VcsScmSaPauseDisabled() ? 1 : 0;
    VcsScmCaptureHudState(state.hud);
    VcsScmCaptureSaGarages(state.garages);
    VcsScmCaptureStatisticsState(state.statistics);
    VcsScmCaptureSaRadar(state.radar);
    VcsScmCaptureSaPopulation(state.population);
}

bool VcsScmValidateHostState(const VcsScmHostState& state)
{
    return state.weatherOld >= 0 && state.weatherOld < 23 &&
        state.weatherNew >= 0 && state.weatherNew < 23 &&
        state.weatherInterpolation >= 0.0f && state.weatherInterpolation <= 1.0f &&
        state.weatherRain >= 0.0f && state.weatherRain <= 1.0f &&
        state.weatherStored <= 1 && state.pauseDisabled <= 1 &&
        VcsScmValidateHudState(state.hud) && VcsScmValidateSaGarages(state.garages) &&
        VcsScmValidateStatisticsState(state.statistics) && VcsScmValidateSaRadar(state.radar) &&
        VcsScmValidateSaPopulation(state.population);
}

bool VcsScmRestoreHostState(const VcsScmHostState& state)
{
    if (!VcsScmValidateHostState(state) || !VcsScmRestoreHudState(state.hud) ||
        !VcsScmRestoreSaGarages(state.garages) || !VcsScmRestoreStatisticsState(state.statistics) ||
        !VcsScmRestoreSaRadar(state.radar) || !VcsScmRestoreSaPopulation(state.population))
        return false;
    savedWeather = state;
    VcsScmSaSetPauseDisabled(state.pauseDisabled != 0);
    return true;
}

void VcsScmBindEnvironment(ReVcsScriptHost& host)
{
    host.clockGet = ClockGet;
    host.clockSet = ClockSet;
    host.clockMinutesUntil = ClockMinutesUntil;
    host.clockStoreRestore = ClockStoreRestore;
    host.scriptTimeScale = TimeScale;
    host.forceWeather = ForceWeather;
    host.forceWeatherGradual = ForceWeatherGradual;
    host.releaseWeather = ReleaseWeather;
    host.weatherSnapshot = StoreWeather;
    host.uniqueTotal = UniqueTotal;
    host.uniqueFound = UniqueFound;
    host.progressTotal = ProgressTotal;
    host.progressMade = ProgressMade;
    host.progressPercentage = ProgressPercentage;
    host.daysPassed = DaysPassed;
    host.killFrenzyTotal = KillFrenzyTotal;
    host.randomU31 = RandomU31;
    host.sceneLoad = SceneLoad;
    host.groundZ3D = GroundZ3D;
    host.pickupGround = PickupGround;
}
