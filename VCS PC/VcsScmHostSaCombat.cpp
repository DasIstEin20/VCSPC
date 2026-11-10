#include "StdAfx.h"
#include "ReVcsScriptVm.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaShared.h"
#include "VcsScmSaCommand.h"
#include "World.h"

namespace {
using Arg = VcsScmSaArg;

bool GiveWeapon(int handle, int sourceType, int ammo)
{
    const int weapon = VcsScmSaWeaponType(sourceType);
    return weapon >= 0 && VcsScmSaPedExists(handle) &&
        VcsScmSaCall(0x01B2, {Arg::Int(handle), Arg::Int(weapon), Arg::Int(ammo)});
}

bool HasWeapon(int handle, int sourceType, bool* result)
{
    const int weapon = VcsScmSaWeaponType(sourceType);
    return result && weapon >= 0 && VcsScmSaPedExists(handle) &&
        VcsScmSaCall(0x0491, {Arg::Int(handle), Arg::Int(weapon)}, result);
}

bool SetWeapon(int handle, int sourceType)
{
    const int weapon = VcsScmSaWeaponType(sourceType);
    return weapon >= 0 && VcsScmSaPedExists(handle) &&
        VcsScmSaCall(0x01B9, {Arg::Int(handle), Arg::Int(weapon)});
}

bool WeaponType(int handle, int* sourceType)
{
    int nativeType = -1;
    if (!sourceType || !VcsScmSaPedExists(handle) ||
        !VcsScmSaCall(0x0470, {Arg::Int(handle), Arg::Out(&nativeType)}))
        return false;
    const int source = VcsScmSaSourceWeapon(nativeType);
    if (source < 0)
        return false;
    *sourceType = source;
    return true;
}

bool Ammo(int handle, int sourceType, int* ammo, bool set)
{
    const int weapon = VcsScmSaWeaponType(sourceType);
    if (!ammo || weapon < 0 || !VcsScmSaPedExists(handle))
        return false;
    if (set)
        return VcsScmSaCall(0x017B, {Arg::Int(handle), Arg::Int(weapon), Arg::Int(*ammo)});
    return VcsScmSaCall(0x041A, {Arg::Int(handle), Arg::Int(weapon), Arg::Out(ammo)});
}

bool AddAmmo(int handle, int sourceType, int ammo)
{
    const int weapon = VcsScmSaWeaponType(sourceType);
    return weapon >= 0 && VcsScmSaPedExists(handle) &&
        VcsScmSaCall(0x0114, {Arg::Int(handle), Arg::Int(weapon), Arg::Int(ammo)});
}

bool ReadNativeSlot(int handle, int slot, int* values)
{
    return VcsScmSaCall(0x04B8, {Arg::Int(handle), Arg::Int(slot),
        Arg::Out(values), Arg::Out(values + 1), Arg::Out(values + 2)});
}

bool ReadSlot(int handle, int sourceSlot, int* values)
{
    if (sourceSlot == 10) {
        int detonator[3];
        if (!ReadNativeSlot(handle, 10, values) || !ReadNativeSlot(handle, 13, detonator))
            return false;
        if (values[0] != 0 && detonator[0] != 0)
            return false; // Two occupied SA slots cannot be represented by one VCS slot.
        if (detonator[0] != 0) {
            values[0] = detonator[0];
            values[1] = detonator[1];
            values[2] = detonator[2];
        }
        return true;
    }
    const int slot = VcsScmSaWeaponSlot(sourceSlot);
    return slot >= 0 && ReadNativeSlot(handle, slot, values);
}

bool WeaponSlot(int handle, int sourceSlot, int* output)
{
    int values[3];
    if (!output || !VcsScmSaPedExists(handle) || !ReadSlot(handle, sourceSlot, values))
        return false;
    const int sourceWeapon = VcsScmSaSourceWeapon(values[0]);
    const int sourceModel = values[2] < 0 ? -1 : VcsScmSourceByModel(values[2]);
    if (sourceWeapon < 0 || (values[2] >= 0 && sourceModel < 0))
        return false;
    output[0] = sourceWeapon;
    output[1] = values[1];
    output[2] = sourceModel;
    return true;
}

bool RemoveWeaponSlot(int handle, int sourceSlot)
{
    if (sourceSlot < 0 || sourceSlot >= 10 || !VcsScmSaPedExists(handle))
        return false;
    int first[3], second[3] = {0, 0, -1};
    if (sourceSlot == 9) {
        if (!ReadNativeSlot(handle, 10, first) || !ReadNativeSlot(handle, 13, second))
            return false;
    } else if (!ReadSlot(handle, sourceSlot + 1, first)) {
        return false;
    }
    // Removing the combined VCS utility slot clears both SA utility owners.
    if (first[0] != 0 && !VcsScmSaCall(0x0555, {Arg::Int(handle), Arg::Int(first[0])}))
        return false;
    return second[0] == 0 || VcsScmSaCall(0x0555, {Arg::Int(handle), Arg::Int(second[0])});
}

bool CombatFlag(unsigned short sourceOpcode, int handle, int value)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    const int enabled = value != 0;
    switch (sourceOpcode) {
    case 0x0339: return VcsScmSaCall(0x054A, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x0232: return VcsScmSaCall(0x039E, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x020E: return VcsScmSaCall(0x0350, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x01AF: return VcsScmSaCall(0x02A9, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x03A5: return VcsScmSaCall(0x087E, {Arg::Int(handle), Arg::Int(enabled)});
    case 0x02F9: return VcsScmSaCall(0x04D8, {Arg::Int(handle), Arg::Int(enabled)});
    default: return false;
    }
}

bool CombatCommand(unsigned short sourceOpcode, int handle, int value)
{
    if (!VcsScmSaPedExists(handle))
        return false;
    switch (sourceOpcode) {
    case 0x01C9: {
        int accuracy = static_cast<int>(static_cast<unsigned char>(value) * 1.25f);
        if (accuracy > 100)
            accuracy = 100;
        return VcsScmSaCall(0x02E2, {Arg::Int(handle), Arg::Int(accuracy)});
    }
    case 0x02A5: return VcsScmSaCall(0x0446, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x032D: return VcsScmSaCall(0x0526, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x0506: return VcsScmSaCall(0x054A, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x01FD: return VcsScmSaCall(0x0332, {Arg::Int(handle), Arg::Int(value != 0)});
    case 0x01F1: return VcsScmSaCall(0x0321, {Arg::Int(handle)});
    case 0x03D5:
        return VcsScmSaCall(0x08AF, {Arg::Int(handle), Arg::Int(value)}) &&
            VcsScmSaCall(0x0223, {Arg::Int(handle), Arg::Int(value)});
    case 0x00E4:
        return VcsScmSaPedExists(value) &&
            VcsScmSaCall(0x05E2, {Arg::Int(handle), Arg::Int(value)});
    case 0x00F4:
        return VcsScmSaPedExists(value) &&
            VcsScmSaCall(0x0635, {Arg::Int(handle), Arg::Int(value), Arg::Int(-1)});
    default: return false;
    }
}

bool LastDamageClear(unsigned short sourceOpcode, int handle)
{
    switch (sourceOpcode) {
    case 0x033B:
        return !VcsScmSaPedExists(handle) || VcsScmSaCall(0x054E, {Arg::Int(handle)});
    case 0x033C:
        return !VcsScmSaVehicleExists(handle) || VcsScmSaCall(0x054F, {Arg::Int(handle)});
    default: return false;
    }
}

bool CombatQuery(unsigned short sourceOpcode, int first, int second, bool* result)
{
    if (!result)
        return false;
    switch (sourceOpcode) {
    case 0x02AE: {
        const int player = VcsScmPlayerSlot(first);
        return player >= 0 && VcsScmSaPedExists(second) &&
            VcsScmSaCall(0x0457, {Arg::Int(player), Arg::Int(second)}, result);
    }
    case 0x0326:
        return VcsScmSaPedExists(first) && VcsScmSaPedExists(second) &&
            VcsScmSaCall(0x051A, {Arg::Int(first), Arg::Int(second)}, result);
    case 0x0327:
        return VcsScmSaPedExists(first) && VcsScmSaVehicleExists(second) &&
            VcsScmSaCall(0x051B, {Arg::Int(first), Arg::Int(second)}, result);
    case 0x0328:
        return VcsScmSaVehicleExists(first) && VcsScmSaPedExists(second) &&
            VcsScmSaCall(0x051C, {Arg::Int(first), Arg::Int(second)}, result);
    case 0x0329:
        return VcsScmSaVehicleExists(first) && VcsScmSaVehicleExists(second) &&
            VcsScmSaCall(0x051D, {Arg::Int(first), Arg::Int(second)}, result);
    case 0x01EE: {
        const int weapon = VcsScmSaDamageType(second);
        return weapon >= 0 && VcsScmSaPedExists(first) &&
            VcsScmSaCall(0x031D, {Arg::Int(first), Arg::Int(weapon)}, result);
    }
    case 0x0336:
        return VcsScmSaPedExists(first) && VcsScmSaVehicleExists(second) &&
            VcsScmSaCall(0x0547, {Arg::Int(first), Arg::Int(second)}, result);
    default: return false;
    }
}

bool PlayerReward(unsigned short sourceOpcode, int handle, int value)
{
    const int slot = VcsScmPlayerSlot(handle);
    if (slot < 0)
        return false;
    switch (sourceOpcode) {
    case 0x01FB: return VcsScmSaCall(0x0330, {Arg::Int(slot), Arg::Int(value != 0)});
    case 0x0340: return VcsScmSaCall(0x055E, {Arg::Int(slot), Arg::Int(static_cast<unsigned char>(value))});
    case 0x0341: return VcsScmSaCall(0x055F, {Arg::Int(slot), Arg::Int(static_cast<unsigned char>(value))});
    default: return false;
    }
}

bool PlayerRestarting()
{
    const int focus = CWorld::PlayerInFocus;
    if (focus < 0 || focus >= NUM_PLAYERS)
        return false;
    // CPlayerInfo::m_nPlayerState in SA PlayerInfo.h. VCSPC exposes the same
    // byte at +0xdc as isWastedBusted. Death and arrest are states 1 and 2.
    const unsigned char state = CWorld::Players[focus].isWastedBusted;
    return state == 1 || state == 2;
}

bool PlayerArrested(int handle, bool* result)
{
    const int slot = VcsScmPlayerSlot(handle);
    if (slot < 0 || !result)
        return false;
    *result = CWorld::Players[slot].isWastedBusted == 2;
    return true;
}
}

void VcsScmBindSaCombat(ReVcsScriptHost& host)
{
    host.characterGiveWeapon = GiveWeapon;
    host.characterHasWeapon = HasWeapon;
    host.characterSetWeapon = SetWeapon;
    host.characterWeaponType = WeaponType;
    host.characterAmmo = Ammo;
    host.characterAddAmmo = AddAmmo;
    host.characterWeaponSlot = WeaponSlot;
    host.characterRemoveWeaponSlot = RemoveWeaponSlot;
    host.characterCombatFlag = CombatFlag;
    host.characterCombatCommand = CombatCommand;
    host.lastDamageEntityClear = LastDamageClear;
    host.combatQuery = CombatQuery;
    host.playerReward = PlayerReward;
    host.playerRestarting = PlayerRestarting;
    host.playerArrested = PlayerArrested;
}
