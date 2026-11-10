#include "VcspcVmApi.h"
#include "ReVcsAssetLayout.h"
#include "ReVcsScriptVm.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <vector>

namespace {

class MissingHostCallback final : public std::exception {
public:
    explicit MissingHostCallback(const char* name) noexcept : name_(name) {}
    const char* what() const noexcept override { return name_; }
private:
    const char* name_;
};

class HostCommandError final : public std::exception {
public:
    explicit HostCommandError(const char* message) noexcept {
        std::snprintf(message_, sizeof(message_), "%s", message);
    }
    const char* what() const noexcept override { return message_; }
private:
    char message_[512];
};

VcspcVmHostErrorProbe hostErrorProbe = nullptr;

void CheckHostError() {
    if (hostErrorProbe) {
        const char* message = hostErrorProbe();
        if (message) throw HostCommandError(message);
    }
}

void VCSPC_VM_CALL SetHostErrorProbe(VcspcVmHostErrorProbe probe) noexcept {
    hostErrorProbe = probe;
}

#include "HostGuards.inc"

// ReVcsScriptVm owns one runtime. The bridge follows that same lifetime and
// remains on the host game thread. Callback functions stay in the host DLL.
struct BridgeState {
    FILE* log = nullptr;
    bool opened = false;
    bool failed = false;
    char message[512] = {};
};
BridgeState state;

void SetMessage(const char* message) noexcept {
    std::snprintf(state.message, sizeof(state.message), "%s", message);
    if (state.log) {
        std::fprintf(state.log, "vcspc_vm_bridge %s\n", state.message);
        std::fflush(state.log);
    }
}

void RecordException() noexcept {
    state.failed = true;
    try {
        throw;
    } catch (const MissingHostCallback& error) {
        char message[512];
        std::snprintf(message, sizeof(message),
                      "Missing VCSPC host callback %s. IP/opcode are the last completed scheduler report",
                      error.what());
        SetMessage(message);
    } catch (const std::exception& error) {
        SetMessage(error.what());
    } catch (...) {
        SetMessage("Unknown exception inside the SCM runtime");
    }
}

bool Ready() noexcept {
    return state.opened && !state.failed;
}

void VCSPC_VM_CALL Close() noexcept {
    try {
        ReVcsCloseScriptVm();
    } catch (...) {
        RecordException();
    }
    if (state.log) std::fclose(state.log);
    state.log = nullptr;
    state.opened = false;
}

int VCSPC_VM_CALL Open(const char* mainScm, const char* logPath,
                       const ReVcsScriptHost* host) noexcept {
    Close();
    state.failed = false;
    state.message[0] = '\0';
    if (!mainScm || !*mainScm || !logPath || !*logPath || !host) {
        SetMessage("SCM path, log path and host callbacks are required");
        return 0;
    }
    state.log = std::fopen(logPath, "a");
    if (!state.log) {
        SetMessage("Could not open the SCM diagnostic log");
        return 0;
    }
    try {
        ReVcsScriptHost guarded = *host;
        // Existing registration gates now install an explicit failing handler
        // for absent engine operations. Guards produce no successful results.
        // Model-name resolution can invoke its guard while loading used objects.
        InstallMissingHostGuards(guarded);
        ReVcsSetDebugOptions(1);
        // Retain the reviewed PS2 layout admission used by the native host.
        // Debug mission assistance remains inactive for the native scheduler.
        state.opened = ReVcsOpenScriptVm(mainScm, state.log, guarded, true, false);
        if (!state.opened) SetMessage("The VCS SCM container or runtime initialization was rejected");
        return state.opened ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL Tick(uint32_t milliseconds, float timeStep) noexcept {
    if (!Ready()) return 0;
    if (!std::isfinite(timeStep) || timeStep < 0.0f || timeStep > 50000.0f) {
        SetMessage("Invalid SCM time step");
        return 0;
    }
    try {
        if (ReVcsTickScriptVm(milliseconds, timeStep)) return 1;
        state.failed = true;
        const ReVcsScriptState vm = ReVcsGetScriptState();
        char message[512];
        std::snprintf(message, sizeof(message),
                      "SCM stopped at IP %u, opcode 0x%04X, VM fault %u",
                      vm.ip, vm.opcode, vm.fault);
        SetMessage(message);
    } catch (...) {
        RecordException();
    }
    return 0;
}

int VCSPC_VM_CALL IsOnMission() noexcept {
    if (!Ready()) return 0;
    try {
        return ReVcsScriptPlayerOnMission() ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL ReadGlobal(uint32_t offset, int32_t* value) noexcept {
    if (!Ready() || !value) return 0;
    try {
        return ReVcsReadScriptGlobal(offset, value) ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL WriteGlobal(uint32_t offset, int32_t value) noexcept {
    if (!Ready()) return 0;
    try {
        return ReVcsWriteScriptGlobal(offset, value) ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

// Story completion history belongs to PlayerInfo in reVCS. Preserve its exact
// existing 28-byte encoding alongside the VM state in this host's envelope.
bool DecodeState(const uint8_t* data, uint32_t bytes,
                 std::vector<uint8_t>& vm, ReVcsStoryProgress& story) {
    if (!data || bytes > VCSPC_VM_MAX_STATE_BYTES) return false;
    revcs_save::Reader reader(data, bytes);
    char magic[8];
    if (!reader.Bytes(magic, sizeof(magic)) || std::memcmp(magic, "VCSPCVM1", 8)) return false;
    const uint32_t fingerprint = reader.U();
    const uint32_t vmSize = reader.U();
    uint8_t storyBytes[ReVcsStoryProgress::Bytes];
    if (!reader.good || fingerprint != VCSPC_VM_HOST_FINGERPRINT ||
        !reader.Bytes(storyBytes, sizeof(storyBytes)) || !story.Read(storyBytes) ||
        reader.pos > reader.size || vmSize != reader.size - reader.pos) return false;
    vm.resize(vmSize);
    return reader.Bytes(vm.data(), vm.size()) && reader.End() &&
           ReVcsScriptLoadState(vm.data(), static_cast<unsigned int>(vm.size()), false, false);
}

uint32_t VCSPC_VM_CALL Save(uint8_t* data, uint32_t capacity, int safehouseMenu) noexcept {
    if (!Ready()) return 0;
    try {
        const uint32_t bytes = ReVcsScriptSaveState(nullptr, 0, safehouseMenu != 0, false);
        constexpr uint32_t envelopeBytes = 8 + 4 + 4 + ReVcsStoryProgress::Bytes + 4;
        if (!bytes || bytes > VCSPC_VM_MAX_STATE_BYTES - envelopeBytes) {
            SetMessage("The VM cannot be saved at its current lifecycle boundary");
            return 0;
        }
        ReVcsStoryProgress story;
        uint8_t storyBytes[ReVcsStoryProgress::Bytes];
        if (!ReVcsGetScriptStoryProgress(&story) || !story.Write(storyBytes)) return 0;
        std::vector<uint8_t> vm(bytes);
        if (ReVcsScriptSaveState(vm.data(), bytes, safehouseMenu != 0, false) != bytes) return 0;
        revcs_save::Writer writer;
        writer.Bytes("VCSPCVM1", 8);
        writer.U(VCSPC_VM_HOST_FINGERPRINT);
        writer.U(bytes);
        writer.Bytes(storyBytes, sizeof(storyBytes));
        writer.Bytes(vm.data(), vm.size());
        writer.Seal();
        if (data) {
            if (capacity < writer.data.size()) return 0;
            std::memcpy(data, writer.data.data(), writer.data.size());
        }
        return static_cast<uint32_t>(writer.data.size());
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL Validate(const uint8_t* data, uint32_t bytes) noexcept {
    if (!Ready()) return 0;
    try {
        std::vector<uint8_t> vm;
        ReVcsStoryProgress story;
        return DecodeState(data, bytes, vm, story) ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL Restore(const uint8_t* data, uint32_t bytes) noexcept {
    if (!Ready()) return 0;
    try {
        std::vector<uint8_t> vm;
        ReVcsStoryProgress story;
        if (!DecodeState(data, bytes, vm, story) ||
            !ReVcsScriptLoadState(vm.data(), static_cast<unsigned int>(vm.size()), true, false)) return 0;
        // DecodeState validated story before the live VM was modified.
        return ReVcsSetScriptStoryProgress(story) ? 1 : 0;
    } catch (...) {
        RecordException();
        return 0;
    }
}

int VCSPC_VM_CALL Diagnostics(VcspcVmDiagnostics* output) noexcept {
    if (!output || output->size != sizeof(*output)) return 0;
    try {
        const ReVcsScriptState vm = ReVcsGetScriptState();
        VcspcVmDiagnostics value = {};
        value.size = sizeof(value);
        value.active = state.opened && vm.active;
        value.failed = state.failed || vm.failed;
        value.entry = vm.entry;
        value.main_bytes = vm.mainBytes;
        value.mission_bytes = vm.missionBytes;
        value.mission_count = vm.missionCount;
        value.passes = vm.passes;
        value.commands = vm.commands;
        value.threads = vm.threads;
        value.instruction_ip = vm.ip;
        value.opcode = vm.opcode;
        value.fault = vm.fault;
        std::snprintf(value.message, sizeof(value.message), "%s", state.message);
        *output = value;
        return 1;
    } catch (...) {
        RecordException();
        return 0;
    }
}

const VcspcVmApi api = {
    sizeof(VcspcVmApi), VCSPC_VM_ABI_VERSION, sizeof(ReVcsScriptHost),
    VCSPC_VM_HOST_FINGERPRINT, &Open, &Close, &Tick, &IsOnMission,
    &Save, &Validate, &Restore, &Diagnostics, &SetHostErrorProbe, &ReadGlobal, &WriteGlobal
};

} // namespace

extern "C" const VcspcVmApi* VCSPC_VM_CALL VcspcVmGetApi(
    uint32_t version, uint32_t hostSize, uint32_t hostFingerprint) {
    if (version != VCSPC_VM_ABI_VERSION || hostSize != sizeof(ReVcsScriptHost) ||
        hostFingerprint != VCSPC_VM_HOST_FINGERPRINT) return nullptr;
    return &api;
}
