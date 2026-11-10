#include "StdAfx.h"
#include "VcsScmHost.h"
#include "VcsScmHostInternal.h"
#include "ReVcsScriptVm.h"
#include "PlayerInfo.h"
#include "World.h"
#include "Pools.h"
#include "Pad.h"
#include "Streaming.h"
#include "PedIntelligence.h"

// SA 1.0 US functions and task size from gta-reversed afbc26d70675b9138.
namespace {
CPlayerInfo* Player(int handle)
{
    const int slot = VcsScmPlayerSlot(handle);
    return slot >= 0 ? &CWorld::Players[slot] : nullptr;
}

int PlayerCharacter(int index)
{
    if (index < 0 || index >= NUM_PLAYERS || !CWorld::Players[index].pPed || !CPools::GetPedPool())
        return -1;
    const int handle = CPools::GetPedPool()->GetHandle(CWorld::Players[index].pPed);
    return VcsScmPlayerSlot(handle) == index ? handle : -1;
}

int CreatePlayer(int index, float x, float y, float z)
{
    if (index < 0 || index >= NUM_PLAYERS || VcsScmModelBySource(0) != 0 ||
        !CPools::GetPedPool() || !CPools::GetTaskPool())
        return -1;
    if (!(x >= -10000.0f && x <= 10000.0f && y >= -10000.0f && y <= 10000.0f &&
        z >= -10000.0f && z <= 10000.0f))
        return -1;
    CPed* existing = CWorld::Players[index].pPed;
    if (existing && (existing->GetModelIndex() != 0 || existing->GetPedFlags().bInVehicle))
        return -1;

    if (CStreaming::ms_aInfoForModel[0].uLoadStatus != StreamingModelLoaded) {
        CStreaming::RequestSpecialModel(0, "player", 0x2 | 0x8 | 0x10);
        CStreaming::LoadAllRequestedModels(true);
        if (CStreaming::ms_aInfoForModel[0].uLoadStatus != StreamingModelLoaded)
            return -1;
    }

    CVector position(x, y, z);
    if (z <= -100.0f) {
        bool found = false;
        position.z = static_cast<float>(CWorld::FindGroundZFor3DCoord(x, y, 1000.0f, &found, nullptr));
        if (!found)
            return -1;
    }

    // Use the native task pool and constructor. Ownership transfers to the
    // existing task manager, including its normal destructor and pool release.
    void* memory = CTask::operator new(0x1C);
    if (!memory)
        return -1;
    CTask* onFoot = reinterpret_cast<CTask* (__thiscall*)(void*)>(0x685750)(memory);
    if (!CWorld::Players[index].pPed)
        reinterpret_cast<void (__cdecl*)(int)>(0x60D790)(index);
    CPed* ped = CWorld::Players[index].pPed;
    if (!ped || !ped->GetPedIntelligencePtr()) {
        delete onFoot;
        return -1;
    }

    position.z += reinterpret_cast<float (__thiscall*)(CEntity*)>(0x536BE0)(ped);
    reinterpret_cast<void (__cdecl*)(int)>(0x609520)(index);
    ped->PedCreatedBy = 2; // MISSION_CHAR in the SA native pool.
    ped->GetCoords() = position;
    ped->m_vecLinearVelocity = CVector(0.0f, 0.0f, 0.0f);
    ped->m_vecAngularVelocity = CVector(0.0f, 0.0f, 0.0f);
    reinterpret_cast<void (__cdecl*)(const CVector&, CEntity*)>(0x486B00)(position, ped);
    reinterpret_cast<void (__cdecl*)(int)>(0x609540)(index);
    reinterpret_cast<void (__thiscall*)(CTaskManager*, CTask*, int, bool)>(0x681AF0)(
        ped->GetPedIntelligencePtr()->GetTaskManager(), onFoot, 4, false);
    return PlayerCharacter(index);
}

bool PlayerControl(int handle, bool enabled)
{
    CPlayerInfo* player = Player(handle);
    if (!player)
        return false;
    reinterpret_cast<void (__thiscall*)(CPlayerInfo*, bool, float)>(0x56E870)(player, !enabled, 10.0f);
    return true;
}

bool PlayerMadeSafe()
{
    // SA's bPlayerSafe is bit 5 of the existing pad control owner.
    return (CPad::GetPad(0)->DisablePlayerControls & 0x20) != 0;
}

bool MoneyGet(int handle, int* money)
{
    CPlayerInfo* player = Player(handle);
    if (!player || !money)
        return false;
    memcpy(money, &player->Score, sizeof(*money));
    return true;
}

bool MoneyAdd(int handle, int delta)
{
    CPlayerInfo* player = Player(handle);
    if (!player)
        return false;
    const unsigned int raw = player->Score + static_cast<unsigned int>(delta);
    int result = 0;
    memcpy(&result, &raw, sizeof(result));
    player->Score = result < 0 ? 0 : static_cast<unsigned int>(result);
    return true;
}

bool WantedLevel(int handle, int* level)
{
    CPlayerInfo* player = Player(handle);
    if (!player || !player->GetPlayerData().m_Wanted || !level)
        return false;
    *level = player->GetPlayerData().m_Wanted->GetWantedLevel();
    return true;
}

bool WantedSet(int handle, int level)
{
    CPlayerInfo* player = Player(handle);
    if (!player || !player->GetPlayerData().m_Wanted)
        return false;
    reinterpret_cast<void (__thiscall*)(CWanted*, int)>(0x562470)(player->GetPlayerData().m_Wanted, level);
    return true;
}

bool WantedNoDrop(int handle, int level)
{
    CPlayerInfo* player = Player(handle);
    if (!player || !player->GetPlayerData().m_Wanted)
        return false;
    reinterpret_cast<void (__thiscall*)(CWanted*, int)>(0x562570)(player->GetPlayerData().m_Wanted, level);
    return true;
}

bool WantedClear(int handle)
{
    return WantedSet(handle, 0);
}

bool WantedGreater(int handle, int threshold)
{
    int level = 0;
    return WantedLevel(handle, &level) && level > threshold;
}

bool WantedStarsFlashing()
{
    CWanted* wanted = CWorld::Players[CWorld::PlayerInFocus].pPed ? FindPlayerWanted(-1) : nullptr;
    return wanted && wanted->GetWantedLevelBeforeParole() > wanted->GetWantedLevel();
}

void MaximumWanted(int level)
{
    reinterpret_cast<void (__cdecl*)(int)>(0x561E70)(level);
}

bool WantedMultiplier(int raw)
{
    CWanted* wanted = FindPlayerWanted(-1);
    if (!wanted)
        return false;
    // m_fMultiplier at +0x14 is also declared in VCSPC's CWanted ABI.
    memcpy(reinterpret_cast<unsigned char*>(wanted) + 0x14, &raw, sizeof(raw));
    return true;
}

void BalloonsBurst(unsigned short count)
{
    CWorld::Players[CWorld::PlayerInFocus].CollectablesPickedUp = count;
}

void BalloonsTotal(unsigned short count)
{
    CWorld::Players[CWorld::PlayerInFocus].TotalNumCollectables = count;
}

bool FireproofReward(int enabled)
{
    CWorld::Players[CWorld::PlayerInFocus].FireProof = enabled != 0;
    return true;
}
}

int VcsScmPlayerSlot(int handle)
{
    if (handle < 0 || (handle & 0x80) || !CPools::GetPedPool())
        return -1;
    CPed* ped = CPools::GetPedPool()->GetAt(handle);
    if (!ped)
        return -1;
    for (int index = 0; index < NUM_PLAYERS; ++index)
        if (CWorld::Players[index].pPed == ped)
            return index;
    return -1;
}

void VcsScmBindPlayer(ReVcsScriptHost& host)
{
    host.playerCreate = CreatePlayer;
    host.playerCharacter = PlayerCharacter;
    host.playerControl = PlayerControl;
    host.playerMadeSafe = PlayerMadeSafe;
    host.playerMoneyGet = MoneyGet;
    host.playerMoneyAdd = MoneyAdd;
    host.playerWantedLevel = WantedLevel;
    host.playerWantedSet = WantedSet;
    host.playerWantedNoDrop = WantedNoDrop;
    host.playerClearWanted = WantedClear;
    host.wantedLevelGreater = WantedGreater;
    host.wantedStarsFlashing = WantedStarsFlashing;
    host.maxWantedLevel = MaximumWanted;
    host.wantedMultiplier = WantedMultiplier;
    host.balloonsBurst = BalloonsBurst;
    host.balloonsTotal = BalloonsTotal;
    host.playerFireproofReward = FireproofReward;
}
