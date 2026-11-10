#include "StdAfx.h"
#include "VcsScmRuntime.h"

#include "FileMgr.h"
#include "Hud.h"
#include "PcSave.h"
#include "Script.h"
#include "Timer.h"
#include "VcsScmHost.h"
#include "VcsScmHostSaGarages.h"
#include "VcsScmHostSaPopulation.h"
#include "VcsScmHostSaRadar.h"
#include "VcsScmSaCommand.h"
#include "VcsScmSaHud.h"
#include "VcsScmSaPresentation.h"
#include "ReVcsScriptVm.h"
#include "VcspcVmApi.h"

#include <algorithm>

namespace {

// gta-reversed/gta-reversed, afbc26d70675b9138b740b857ffdcaeaa7c5ca7c,
// GenericGameStorage.h and GenericGameStorage.cpp. Both transaction loops
// inspect this flag immediately after the script block.
bool& storageFailed = *reinterpret_cast<bool*>(0xC16EFC);
bool& storageLoading = *reinterpret_cast<bool*>(0xC16EFD);
unsigned int& storageBufferSize = *reinterpret_cast<unsigned int*>(0x8D2BE0);
int& storageBufferPosition = *reinterpret_cast<int*>(0xC16EEC);

const unsigned int stateMagic = 0x4D565356; // VSVM, little endian
const unsigned int stateVersion = 2;

struct StateHeader {
    unsigned int magic;
    unsigned int version;
    unsigned int bytes;
    unsigned int checksum;
    unsigned int hostFingerprint;
    unsigned int phase;
    float elapsedStep;
    VcsScmHostState hostState;
};
static_assert(sizeof(StateHeader) == 28 + sizeof(VcsScmHostState), "SCM save header layout changed");

struct Runtime {
    bool configured = false;
    bool enabled = false;
    bool configurationValid = false;
    bool started = false;
    bool failed = false;
    bool frameSeen = false;
    bool savePrepared = false;
    bool loadPending = false;
    bool nativeHooksInstalled = false;
    HMODULE module = nullptr;
    const VcspcVmApi* api = nullptr;
    FILE* log = nullptr;
    DWORD ownerThread = 0;
    ReVcsScriptHost host = {};
    char directory[MAX_PATH] = {};
    char ini[MAX_PATH] = {};
    char script[MAX_PATH] = {};
    char dll[MAX_PATH] = {};
    char models[MAX_PATH] = {};
    char vmLog[MAX_PATH] = {};
    char hostLog[MAX_PATH] = {};
    unsigned int wallClock = 0;
    unsigned int phase = 0;
    unsigned int lastFrame = 0;
    float elapsedStep = 0.0f;
    StateHeader saveHeader = {};
    StateHeader loadHeader = {};
    std::vector<unsigned char> saveState;
    std::vector<unsigned char> loadState;
};

Runtime runtime;

void Report(const char* message, bool stop)
{
    LogToFile("SCM VM: %s", message);
    if (runtime.log) {
        fprintf(runtime.log, "runtime %s\n", message);
        fflush(runtime.log);
    }
    if (stop) {
        runtime.failed = true;
        runtime.started = false;
    }
}

void ReportVmFailure(const char* operation, bool stop)
{
    VcspcVmDiagnostics diagnostic = {};
    diagnostic.size = sizeof(diagnostic);
    char message[768];
    if (runtime.api && runtime.api->diagnostics(&diagnostic)) {
        diagnostic.message[sizeof(diagnostic.message) - 1] = '\0';
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "%s failed, ip=%u opcode=0x%04X fault=%u, %s", operation,
            diagnostic.instruction_ip, diagnostic.opcode, diagnostic.fault,
            diagnostic.message);
    } else {
        _snprintf_s(message, sizeof(message), _TRUNCATE, "%s failed", operation);
    }
    Report(message, stop);
}

bool CheckHostFailure()
{
    const char* error = VcsScmSaError();
    if (!error)
        return true;
    Report(error, true);
    return false;
}

bool MatchNativeBytes(unsigned int address, const unsigned char* expected, unsigned int bytes)
{
    if (memcmp(reinterpret_cast<const void*>(address), expected, bytes) == 0)
        return true;
    char message[128];
    _snprintf_s(message, sizeof(message), _TRUNCATE,
        "native code at 0x%08X differs from the reviewed gta-vcs.exe", address);
    Report(message, true);
    return false;
}

void RunPass(const char* operation, float timeStep)
{
    if (!CheckHostFailure())
        return;
    VcsScmSaPresentationBeginPass();
    if (!CheckHostFailure())
        return;
    VcsScmSaMarkersBeginPass();
    if (!CheckHostFailure())
        return;
    const bool completed = runtime.api->tick(
        static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds), timeStep) != 0;
    if (CheckHostFailure() && !completed)
        ReportVmFailure(operation, true);
}

bool AbsolutePath(const char* value, char* result)
{
    char joined[MAX_PATH];
    const int length = _snprintf_s(joined, sizeof(joined), _TRUNCATE, "%s%s",
        PathIsRelativeA(value) ? runtime.directory : "", value);
    if (length < 0)
        return false;
    const DWORD written = GetFullPathNameA(joined, MAX_PATH, result, nullptr);
    return written > 0 && written < MAX_PATH;
}

bool ConfigPath(const char* key, const char* fallback, char* result)
{
    char value[MAX_PATH];
    const DWORD written = GetPrivateProfileStringA("ScriptVM", key, fallback,
        value, sizeof(value), runtime.ini);
    return written > 0 && written < sizeof(value) - 1 && AbsolutePath(value, result);
}

void ReadConfiguration()
{
    if (runtime.configured)
        return;
    runtime.configured = true;
    const DWORD written = GetModuleFileNameA(nullptr, runtime.directory, MAX_PATH);
    if (!written || written >= MAX_PATH)
        return;
    char* separator = strrchr(runtime.directory, '\\');
    if (!separator)
        return;
    separator[1] = '\0';
    if (!AbsolutePath("VCSPC.ini", runtime.ini))
        return;
    runtime.enabled = GetPrivateProfileIntA("ScriptVM", "enabled", 0, runtime.ini) != 0;
    if (!runtime.enabled)
        return;
    runtime.configurationValid =
        ConfigPath("path", "data\\script\\vcs_ps2\\main.scm", runtime.script) &&
        ConfigPath("dll", "VCSPC.ScmVm.dll", runtime.dll) &&
        ConfigPath("modelMap", "data\\script\\vcs_ps2\\models.tsv", runtime.models) &&
        ConfigPath("log", "vcspc-scm-vm.log", runtime.vmLog) &&
        AbsolutePath("vcspc-scm-host.log", runtime.hostLog);
    char stockScript[MAX_PATH];
    if (!AbsolutePath("data\\script\\main.scm", stockScript) ||
        _stricmp(runtime.script, stockScript) == 0) {
        runtime.configurationValid = false;
        Report("ScriptVM.path must name a separate original PS2 SCM file", true);
    }
}

void ResetClock()
{
    runtime.wallClock = GetTickCount();
    runtime.phase = 0;
    runtime.elapsedStep = 0.0f;
    runtime.frameSeen = false;
}

unsigned int Checksum(const std::vector<unsigned char>& bytes, const VcsScmHostState& hostState)
{
    unsigned int checksum = 2166136261u;
    const unsigned char* hostBytes = reinterpret_cast<const unsigned char*>(&hostState);
    for (unsigned int i = 0; i < sizeof(hostState); ++i)
        checksum = (checksum ^ hostBytes[i]) * 16777619u;
    for (unsigned char value : bytes)
        checksum = (checksum ^ value) * 16777619u;
    return checksum;
}

bool PrepareSave()
{
    runtime.savePrepared = false;
    runtime.saveState.clear();
    if (!CheckHostFailure() || !VcsScmReady() || !runtime.started || runtime.loadPending) {
        Report("save rejected because the SCM session is unavailable", false);
        return false;
    }
    const unsigned int bytes = runtime.api->save(nullptr, 0, 1);
    if (!bytes || bytes > VCSPC_VM_MAX_STATE_BYTES) {
        ReportVmFailure("save preflight", false);
        return false;
    }
    try {
        runtime.saveState.resize(bytes);
    } catch (...) {
        Report("save rejected because state allocation failed", false);
        return false;
    }
    if (runtime.api->save(runtime.saveState.data(), bytes, 1) != bytes ||
        !runtime.api->validate(runtime.saveState.data(), bytes)) {
        runtime.saveState.clear();
        ReportVmFailure("save validation", false);
        return false;
    }
    VcsScmHostState hostState = {};
    VcsScmCaptureHostState(hostState);
    if (!VcsScmValidateHostState(hostState) || !VcsScmValidateHudGlobals(hostState.hud) ||
        !VcsScmValidateSaPopulationModels(hostState.population)) {
        runtime.saveState.clear();
        Report("save rejected because adapter state is invalid", false);
        return false;
    }
    runtime.saveHeader = {stateMagic, stateVersion, bytes, Checksum(runtime.saveState, hostState),
        VCSPC_VM_HOST_FINGERPRINT, runtime.phase, runtime.elapsedStep, hostState};
    runtime.savePrepared = true;
    return true;
}

// SaveSlot's __thiscall ABI is bridged by the unused EDX argument. The stock
// routine's complete body is documented in gta-reversed C_PcSave.cpp at
// 0x619060. Preflight runs before GenericSave opens the slot with "wb".
unsigned int __fastcall SaveSlot(void*, void*, int slot)
{
    if (VcsScmEnabled() && !PrepareSave())
        return 2;
    reinterpret_cast<void(__cdecl*)(int)>(0x5D0E90)(slot);
    *reinterpret_cast<unsigned int*>(0xC17034) = 0;
    CFileMgr::SetDirMyDocuments();
    reinterpret_cast<void(__cdecl*)()>(0x618F50)();
    const bool saved = reinterpret_cast<bool(__cdecl*)(bool)>(0x5D13E0)(false);
    runtime.savePrepared = false;
    runtime.saveState.clear();
    return saved ? 0 : 2;
}

bool TransferState(bool load, unsigned char* data, unsigned int bytes)
{
    // SA crosses one boundary per call, then copies the remainder without
    // checking the size of a short final refill. Consume only the current
    // buffer. One byte triggers a refill at its end. LoadWorkBuffer returns
    // false at EOF, so that single-byte request cannot cross a short refill.
    while (bytes) {
        if (storageFailed || !storageBufferSize || storageBufferPosition < 0 ||
            static_cast<unsigned int>(storageBufferPosition) > storageBufferSize)
            return false;
        const unsigned int remaining = storageBufferSize - storageBufferPosition;
        const unsigned int count = std::min(bytes, std::min(4096u, remaining ? remaining : 1u));
        const bool success = load ? C_PcSave::PcClassLoadRoutine(data, count) :
            C_PcSave::PcClassSaveRoutine(data, count);
        if (!success || storageBufferPosition < 0 ||
            static_cast<unsigned int>(storageBufferPosition) > storageBufferSize)
            return false;
        data += count;
        bytes -= count;
    }
    return true;
}

} // namespace

bool VcsScmEnabled()
{
    ReadConfiguration();
    return runtime.enabled;
}

bool VcsScmReady()
{
    return runtime.api && !runtime.failed && CheckHostFailure();
}

void VcsScmShutdown()
{
    // Emergency-key termination runs on a worker and immediately exits the
    // process. Session teardown belongs to the game thread that owns ticks.
    if (runtime.ownerThread && runtime.ownerThread != GetCurrentThreadId())
        return;
    if (runtime.api)
        runtime.api->close();
    runtime.api = nullptr;
    VcsScmResetHost();
    VcsScmSaSetPauseDisabled(false);
    VcsScmSaReset();
    if (runtime.module)
        FreeLibrary(runtime.module);
    runtime.module = nullptr;
    if (runtime.log)
        fclose(runtime.log);
    runtime.log = nullptr;
    runtime.ownerThread = 0;
    runtime.started = false;
    runtime.savePrepared = false;
    runtime.loadPending = false;
    runtime.saveState.clear();
    runtime.loadState.clear();
    ResetClock();
}

void VcsScmInitialise()
{
    VcsScmShutdown();
    runtime.failed = false;
    if (!runtime.nativeHooksInstalled || !VcsScmSaVerifyNative()) {
        Report("SCM native hooks are unavailable for this executable layout", true);
        return;
    }
    if (!runtime.configurationValid) {
        Report("invalid ScriptVM configuration", true);
        return;
    }
    runtime.ownerThread = GetCurrentThreadId();
    runtime.log = fopen(runtime.hostLog, "a");
    if (!runtime.log) {
        Report("cannot open vcspc-scm-host.log", true);
        return;
    }
    // The host still initializes native script-owned engine resources using
    // its own SCM metadata. Its main thread is replaced by VcsScmStart.
    TheScriptsInitFunc();
    if (!VcsScmLoadModelMap(runtime.models, runtime.log)) {
        Report("source model map could not be resolved against loaded IDE data", true);
        return;
    }
    runtime.module = LoadLibraryA(runtime.dll);
    if (!runtime.module) {
        char message[MAX_PATH + 80];
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "cannot load VM DLL %s, Win32 error=%lu", runtime.dll, GetLastError());
        Report(message, true);
        return;
    }
    const VcspcVmGetApiFn getApi = reinterpret_cast<VcspcVmGetApiFn>(
        GetProcAddress(runtime.module, "VcspcVmGetApi"));
    const VcspcVmApi* api = getApi ? getApi(VCSPC_VM_ABI_VERSION,
        sizeof(ReVcsScriptHost), VCSPC_VM_HOST_FINGERPRINT) : nullptr;
    if (!api || api->size != sizeof(VcspcVmApi) || api->version != VCSPC_VM_ABI_VERSION ||
        api->host_size != sizeof(ReVcsScriptHost) ||
        api->host_fingerprint != VCSPC_VM_HOST_FINGERPRINT ||
        !api->open || !api->close || !api->tick || !api->is_on_mission ||
        !api->save || !api->validate || !api->restore || !api->diagnostics ||
        !api->set_host_error_probe || !api->read_global || !api->write_global) {
        Report("VM DLL has an incompatible callback ABI", true);
        return;
    }
    runtime.api = api;
    VcsScmBuildHost(runtime.host);
    api->set_host_error_probe(&VcsScmSaError);
    if (!api->open(runtime.script, runtime.vmLog, &runtime.host)) {
        ReportVmFailure("open original SCM", true);
        return;
    }
    ResetClock();
    Report("original SCM ready, cadence=30Hz, clock=scaled simulation, gate=unscaled", false);
}

void VcsScmStart()
{
    if (!VcsScmReady() || runtime.started || runtime.loadPending)
        return;
    runtime.started = true;
    ResetClock();
    // Bootstrap creates the player before the game's first camera update.
    runtime.lastFrame = CTimer::m_FrameCounter;
    runtime.frameSeen = true;
    RunPass("bootstrap", 0.0f);
}

void VcsScmObserveFrame()
{
    if (VcsScmEnabled() && (CTimer::m_UserPause || CTimer::m_CodePause)) {
        runtime.wallClock = GetTickCount();
        runtime.frameSeen = false;
    }
}

void VcsScmProcess()
{
    if (!VcsScmReady() || storageLoading || !CheckHostFailure())
        return;
    if (runtime.loadPending) {
        runtime.loadPending = false;
        if (storageFailed || !VcsScmValidateHudGlobals(runtime.loadHeader.hostState.hud) ||
            !VcsScmValidateSaPopulationModels(runtime.loadHeader.hostState.population)) {
            runtime.loadState.clear();
            storageFailed = true;
            Report("saved adapter references unavailable VM globals or native models", true);
            return;
        }
        if (!runtime.api->restore(runtime.loadState.data(),
                static_cast<unsigned int>(runtime.loadState.size()))) {
            runtime.loadState.clear();
            storageFailed = true;
            ReportVmFailure("restore after native pools", true);
            return;
        }
        runtime.loadState.clear();
        VcsScmSaResetInputQueries();
        if (!VcsScmRestoreHostState(runtime.loadHeader.hostState)) {
            storageFailed = true;
            Report("adapter state restore failed after native pools", true);
            return;
        }
        runtime.started = true;
        ResetClock();
        runtime.phase = runtime.loadHeader.phase;
        runtime.elapsedStep = runtime.loadHeader.elapsedStep;
    }
    if (!runtime.started || (runtime.frameSeen && runtime.lastFrame == CTimer::m_FrameCounter))
        return;
    runtime.frameSeen = true;
    runtime.lastFrame = CTimer::m_FrameCounter;
    if (!(CTimer::ms_fTimeStep >= 0.0f && CTimer::ms_fTimeStep <= 50000.0f)) {
        Report("invalid native simulation time step", true);
        return;
    }
    runtime.elapsedStep += CTimer::ms_fTimeStep;
    const unsigned int wallClock = GetTickCount();
    const unsigned int elapsed = wallClock - runtime.wallClock;
    runtime.wallClock = wallClock;
    const unsigned long long phase = runtime.phase + static_cast<unsigned long long>(elapsed) * 30u;
    runtime.phase = static_cast<unsigned int>(phase % 1000u);
    if (phase < 1000u)
        return;
    const float step = runtime.elapsedStep;
    runtime.elapsedStep = 0.0f;
    RunPass("script pass", step);
}

bool VcsScmIsPlayerOnMission()
{
    return VcsScmReady() && runtime.api->is_on_mission() != 0;
}

bool VcsScmReadGlobal(unsigned int offset, int* value)
{
    int32_t result;
    if (!VcsScmReady() || !value || !runtime.api->read_global(offset, &result)) {
        char message[128];
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "cannot read SCM global at byte offset %u", offset);
        VcsScmSaReject(message);
        Report(message, true);
        return false;
    }
    *value = result;
    return true;
}

bool VcsScmWriteGlobal(unsigned int offset, int value)
{
    if (!VcsScmReady() || !runtime.api->write_global(offset, value)) {
        char message[128];
        _snprintf_s(message, sizeof(message), _TRUNCATE,
            "cannot write SCM global at byte offset %u", offset);
        VcsScmSaReject(message);
        Report(message, true);
        return false;
    }
    return true;
}

void VcsScmSave()
{
    if ((!runtime.savePrepared && !PrepareSave()) ||
        !TransferState(false, reinterpret_cast<unsigned char*>(&runtime.saveHeader), sizeof(runtime.saveHeader)) ||
        !TransferState(false, runtime.saveState.data(), static_cast<unsigned int>(runtime.saveState.size()))) {
        storageFailed = true;
        Report("SCM state save transaction failed", false);
    }
    runtime.savePrepared = false;
    runtime.saveState.clear();
}

void VcsScmLoad()
{
    StateHeader header = {};
    if (!VcsScmReady() || !TransferState(true, reinterpret_cast<unsigned char*>(&header), sizeof(header)) ||
        header.magic != stateMagic || header.version != stateVersion ||
        !header.bytes || header.bytes > VCSPC_VM_MAX_STATE_BYTES ||
        header.hostFingerprint != VCSPC_VM_HOST_FINGERPRINT || header.phase >= 1000u ||
        !(header.elapsedStep >= 0.0f && header.elapsedStep <= 50000.0f) ||
        !VcsScmValidateHostState(header.hostState)) {
        storageFailed = true;
        Report("invalid or incompatible SCM state header", true);
        return;
    }
    try {
        runtime.loadState.resize(header.bytes);
    } catch (...) {
        storageFailed = true;
        Report("SCM load state allocation failed", true);
        return;
    }
    if (!TransferState(true, runtime.loadState.data(), header.bytes) ||
        Checksum(runtime.loadState, header.hostState) != header.checksum ||
        !runtime.api->validate(runtime.loadState.data(), header.bytes)) {
        runtime.loadState.clear();
        storageFailed = true;
        ReportVmFailure("load state validation", true);
        return;
    }
    runtime.loadHeader = header;
    runtime.loadPending = true;
    runtime.started = false;
}

void VcsScmInstallHooks()
{
    if (!VcsScmEnabled() || runtime.nativeHooksInstalled)
        return;
    if (!VcsScmSaVerifyNative()) {
        CheckHostFailure();
        return;
    }
    // Byte signatures from the supplied executable identify the exact code
    // replaced below. Validate every site before installing any new hook.
    const unsigned char restart[] = {0xE8, 0x74, 0xCF, 0xF2, 0xFF};
    const unsigned char start[] = {0x68, 0x60, 0x99, 0xA4, 0x00, 0xE8, 0xD6, 0xFE, 0xFF, 0xFF, 0x59, 0xC3};
    const unsigned char mission[] = {0xA1, 0xAC, 0x76, 0xA4, 0x00, 0x85, 0xC0, 0x74, 0x0C};
    const unsigned char save[] = {0x0F, 0xBE, 0x44, 0x24, 0x04, 0x50, 0xE8, 0x25, 0x7E, 0xFB, 0xFF};
    const unsigned char saveBuffer[] = {0xA0, 0xFC, 0x6E, 0xC1, 0x00, 0x84, 0xC0, 0x74, 0x03, 0x32, 0xC0, 0xC3};
    const unsigned char drawFadeFirst[] = {0xE8, 0x3B, 0xEF, 0x04, 0x00};
    const unsigned char drawFadeSecond[] = {0xE8, 0x4F, 0xEC, 0x04, 0x00};
    const unsigned char drawFadeThird[] = {0xE8, 0xE9, 0xE8, 0x04, 0x00};
    if (!MatchNativeBytes(0x53BDD7, restart, sizeof(restart)) ||
        !MatchNativeBytes(0x464D40, start, sizeof(start)) ||
        !MatchNativeBytes(0x464D50, mission, sizeof(mission)) ||
        !MatchNativeBytes(0x619060, save, sizeof(save)) ||
        !MatchNativeBytes(0x5D1270, saveBuffer, sizeof(saveBuffer)) ||
        !MatchNativeBytes(0x5D1300, saveBuffer, sizeof(saveBuffer)) ||
        !MatchNativeBytes(0x53E550, drawFadeFirst, sizeof(drawFadeFirst)) ||
        !MatchNativeBytes(0x53E83C, drawFadeSecond, sizeof(drawFadeSecond)) ||
        !MatchNativeBytes(0x53EBA2, drawFadeThird, sizeof(drawFadeThird)))
        return;
    if (!VcsScmInstallSaGarageHooks()) {
        Report("garage hook sites differ from the reviewed gta-vcs.exe", true);
        return;
    }
    if (!VcsScmInstallSaRadarHooks()) {
        Report("radar hook sites differ from the reviewed gta-vcs.exe", true);
        return;
    }
    if (!VcsScmInstallSaPopulationHooks()) {
        Report("population hook sites differ from the reviewed gta-vcs.exe", true);
        return;
    }
    // Called after InjectDelayedPatches captured the original initializer.
    Memory::InjectHook(0x53BDD7, &CTheScripts::Init, PATCH_CALL);
    Memory::InjectHook(0x464D40, &CTheScripts::StartTestScript, PATCH_JUMP);
    Memory::InjectHook(0x464D50, &CTheScripts::IsPlayerOnAMission, PATCH_JUMP);
    Memory::InjectHook(0x619060, &SaveSlot, PATCH_JUMP);
    Memory::InjectHook(0x53E550, &CHud::DrawAfterFade, PATCH_CALL);
    Memory::InjectHook(0x53E83C, &CHud::DrawAfterFade, PATCH_CALL);
    Memory::InjectHook(0x53EBA2, &CHud::DrawAfterFade, PATCH_CALL);
    runtime.nativeHooksInstalled = true;
}
