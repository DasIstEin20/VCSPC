#include "StdAfx.h"
#include "VcsScmHostSaPopulation.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "ModelInfo.h"
#include "Streaming.h"
#include "ReVcsScriptVm.h"

namespace {

// Pinned SA Population.h/PopCycle.h and Streaming.h. Gang groups start at 18,
// with three world-zone translations into the 57 native ped groups.
int (*const translations)[3] = reinterpret_cast<int (*)[3]>(0x8D2540);
short (*const pedGroups)[21] = reinterpret_cast<short (*)[21]>(0xC0F358);
unsigned short* const pedCounts = reinterpret_cast<unsigned short*>(0xC0ECC0);
short (*const carGroups)[23] = reinterpret_cast<short (*)[23]>(0xC0ED38);
unsigned short* const carCounts = reinterpret_cast<unsigned short*>(0xC0EC78);
short (*const loadedGangCars)[23] = reinterpret_cast<short (*)[23]>(0xC0EA28);
unsigned short& loadedPeds = *reinterpret_cast<unsigned short*>(0x8E4BAC);
unsigned short& loadedCars = *reinterpret_cast<unsigned short*>(0x8E4BA8);

struct PedGroupBackup {
    bool active;
    unsigned short count;
    short models[21];
};
struct CarGroupBackup {
    bool active;
    unsigned short count;
    short models[23];
};
PedGroupBackup pedBackups[57];
CarGroupBackup carBackups[9];
VcsScmPopulationState population;
unsigned short changedPedGangs;
bool hooksInstalled;

bool ModelKind(int model, unsigned char kind)
{
    if (model < 0 || model >= 20000)
        return false;
    CBaseModelInfo* info = CModelInfo::GetModelInfo(static_cast<unsigned short>(model));
    return info && info->GetModelType() == kind;
}

bool LoadedVehicle(int model)
{
    return ModelKind(model, 6) && CStreaming::ms_aInfoForModel[model].uLoadStatus == StreamingModelLoaded;
}

bool PedGroupIndices(int gang, int* groups)
{
    for (unsigned int zone = 0; zone < 3; ++zone) {
        groups[zone] = translations[18 + gang][zone];
        if (groups[zone] < 0 || groups[zone] >= 57 || pedCounts[groups[zone]] > 21)
            return false;
    }
    return true;
}

void RebuildLoadedCars(int gang)
{
    memset(loadedGangCars[gang], 0xFF, sizeof(loadedGangCars[gang]));
    unsigned int count = 0;
    const int group = 18 + gang;
    for (unsigned int i = 0; i < carCounts[group]; ++i) {
        const int model = carGroups[group][i];
        if (LoadedVehicle(model))
            loadedGangCars[gang][count++] = static_cast<short>(model);
    }
    loadedCars &= static_cast<unsigned short>(~(1u << gang));
}

void ApplyPedModels(int gang, int first, int second, const int* groups)
{
    for (unsigned int zone = 0; zone < 3; ++zone) {
        const int group = groups[zone];
        PedGroupBackup& backup = pedBackups[group];
        if (!backup.active) {
            backup.count = pedCounts[group];
            memcpy(backup.models, pedGroups[group], sizeof(backup.models));
            backup.active = true;
        }
        pedGroups[group][0] = static_cast<short>(first);
        pedGroups[group][1] = static_cast<short>(second);
        pedCounts[group] = 2;
    }
    population.pedModels[gang][0] = first;
    population.pedModels[gang][1] = second;
    changedPedGangs |= static_cast<unsigned short>(1u << gang);
    loadedPeds &= static_cast<unsigned short>(~(1u << gang));
}

void ApplyVehicleModel(int gang, int model)
{
    const int group = 18 + gang;
    CarGroupBackup& backup = carBackups[gang];
    if (!backup.active) {
        backup.count = carCounts[group];
        memcpy(backup.models, carGroups[group], sizeof(backup.models));
        backup.active = true;
    }
    population.vehicleModels[gang] = model;
    if (model >= 0) {
        carGroups[group][0] = static_cast<short>(model);
        carCounts[group] = 1;
        RebuildLoadedCars(gang);
    }
    // For model=-1 the guarded PickGangCar hook suppresses selection. Keep a
    // valid native group because StreamZoneModels_Gangs divides by its count.
}

int __cdecl PickGangCar(int gang)
{
    if (gang < 0 || gang >= 10)
        return -1;
    if (gang < 9 && population.vehicleModels[gang] != -2) {
        const int model = population.vehicleModels[gang];
        return LoadedVehicle(model) ? model : -1;
    }
    // Complete original 0x614490 body, including the native loaded-group owner.
    return reinterpret_cast<int (__thiscall*)(void*, bool, bool)>(0x611C50)(
        loadedGangCars[gang], false, false);
}

void __cdecl StreamGangModels(const void* position)
{
    // A disabled VCS gang car has no model to request. Both verified native
    // callers pass through here, preventing the zero-count modulo operation
    // for a disabled group without putting a fake model in the loaded cache.
    for (unsigned int gang = 0; gang < 9; ++gang) {
        if (population.vehicleModels[gang] == -1)
            loadedCars |= static_cast<unsigned short>(1u << gang);
    }
    reinterpret_cast<void (__cdecl*)(const void*)>(0x40AA10)(position);
}

bool GangPedModels(short gang, int firstSource, int secondSource)
{
    if (!hooksInstalled || gang < 0 || gang >= 9)
        return false;
    const int first = VcsScmModelBySource(firstSource);
    const int second = VcsScmModelBySource(secondSource);
    int groups[3];
    if (!ModelKind(first, 7) || !ModelKind(second, 7) || !PedGroupIndices(gang, groups))
        return false;
    ApplyPedModels(gang, first, second, groups);
    return true;
}

bool GangVehicleModel(short gang, int source)
{
    if (!hooksInstalled || gang < 0 || gang >= 9 || carCounts[18 + gang] > 23)
        return false;
    const int model = source == -1 ? -1 : VcsScmModelBySource(source);
    if (source != -1 && !ModelKind(model, 6))
        return false;
    ApplyVehicleModel(gang, model);
    return true;
}

bool GangWeapons(short gang, int first, int second)
{
    const int a = VcsScmSaWeaponType(first);
    const int b = VcsScmSaWeaponType(second);
    if (gang < 0 || gang >= 9 || a < 0 || b < 0)
        return false;
    typedef VcsScmSaArg Arg;
    return VcsScmSaCall(0x0237, {Arg::Int(gang), Arg::Int(a), Arg::Int(b), Arg::Int(0)});
}

} // namespace

bool VcsScmInstallSaPopulationHooks()
{
    if (hooksInstalled)
        return true;
    const unsigned char original[] = {
        0x8B, 0x4C, 0x24, 0x04, 0x6B, 0xC9, 0x2E, 0x6A, 0x00, 0x6A, 0x00,
        0x81, 0xC1, 0x28, 0xEA, 0xC0, 0x00, 0xE8, 0xAA, 0xD7, 0xFF, 0xFF, 0xC3
    };
    const unsigned char zoneStreamCall[] = {0xE8, 0x52, 0xF3, 0xFF, 0xFF};
    const unsigned char trafficStreamCall[] = {0xE8, 0xE3, 0x3A, 0xFC, 0xFF};
    if (!VcsScmSaVerifyNative() || memcmp(reinterpret_cast<void*>(0x614490), original, sizeof(original)) ||
        memcmp(reinterpret_cast<void*>(0x40B6B9), zoneStreamCall, sizeof(zoneStreamCall)) ||
        memcmp(reinterpret_cast<void*>(0x446F28), trafficStreamCall, sizeof(trafficStreamCall)))
        return false;
    VcsScmResetSaPopulation();
    Memory::InjectHook(0x614490, &PickGangCar, PATCH_JUMP);
    Memory::InjectHook(0x40B6B9, &StreamGangModels, PATCH_CALL);
    Memory::InjectHook(0x446F28, &StreamGangModels, PATCH_CALL);
    hooksInstalled = true;
    return true;
}

void VcsScmResetSaPopulation()
{
    for (unsigned int group = 0; group < 57; ++group) {
        PedGroupBackup& backup = pedBackups[group];
        if (backup.active) {
            pedCounts[group] = backup.count;
            memcpy(pedGroups[group], backup.models, sizeof(backup.models));
            backup.active = false;
        }
    }
    if (changedPedGangs)
        loadedPeds &= static_cast<unsigned short>(~changedPedGangs);
    changedPedGangs = 0;
    for (unsigned int gang = 0; gang < 9; ++gang) {
        CarGroupBackup& backup = carBackups[gang];
        if (backup.active) {
            carCounts[18 + gang] = backup.count;
            memcpy(carGroups[18 + gang], backup.models, sizeof(backup.models));
            RebuildLoadedCars(gang);
            backup.active = false;
        }
        population.pedModels[gang][0] = population.pedModels[gang][1] = -1;
        population.vehicleModels[gang] = -2;
    }
}

void VcsScmCaptureSaPopulation(VcsScmPopulationState& state) { state = population; }

bool VcsScmValidateSaPopulation(const VcsScmPopulationState& state)
{
    for (unsigned int gang = 0; gang < 9; ++gang) {
        const int first = state.pedModels[gang][0], second = state.pedModels[gang][1];
        if (first < -1 || first >= 20000 || second < -1 || second >= 20000 ||
            ((first == -1) != (second == -1)) ||
            state.vehicleModels[gang] < -2 || state.vehicleModels[gang] >= 20000)
            return false;
    }
    return true;
}

bool VcsScmValidateSaPopulationModels(const VcsScmPopulationState& state)
{
    if (!VcsScmValidateSaPopulation(state))
        return false;
    for (unsigned int gang = 0; gang < 9; ++gang) {
        int groups[3];
        if (state.pedModels[gang][0] != -1 &&
            (!ModelKind(state.pedModels[gang][0], 7) || !ModelKind(state.pedModels[gang][1], 7) ||
             !PedGroupIndices(gang, groups)))
            return false;
        const int model = state.vehicleModels[gang];
        if ((model >= 0 && !ModelKind(model, 6)) || carCounts[18 + gang] > 23)
            return false;
    }
    return true;
}

bool VcsScmRestoreSaPopulation(const VcsScmPopulationState& state)
{
    if (!hooksInstalled || !VcsScmValidateSaPopulationModels(state))
        return false;
    const VcsScmPopulationState saved = state;
    VcsScmResetSaPopulation();
    for (unsigned int gang = 0; gang < 9; ++gang) {
        if (saved.pedModels[gang][0] != -1) {
            int groups[3];
            if (!PedGroupIndices(gang, groups))
                return false;
            ApplyPedModels(gang, saved.pedModels[gang][0], saved.pedModels[gang][1], groups);
        }
        if (saved.vehicleModels[gang] != -2)
            ApplyVehicleModel(gang, saved.vehicleModels[gang]);
    }
    return true;
}

void VcsScmBindSaPopulation(ReVcsScriptHost& host)
{
    host.gangPedModels = GangPedModels;
    host.gangVehicleModel = GangVehicleModel;
    host.gangWeapons = GangWeapons;
}
