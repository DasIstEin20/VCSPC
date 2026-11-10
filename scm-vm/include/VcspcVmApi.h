#ifndef VCSPC_VM_API_H
#define VCSPC_VM_API_H

#include <stdint.h>

struct ReVcsScriptHost;

#if defined(_WIN32)
#define VCSPC_VM_CALL __cdecl
#else
#define VCSPC_VM_CALL
#endif

// Both modules use the same source-level callback contract. No FILE*, STL
// object, allocation or C++ exception crosses this compiler/CRT boundary.
#define VCSPC_VM_ABI_VERSION 2u
#define VCSPC_VM_HOST_FINGERPRINT 0x5085AC04u
#define VCSPC_VM_MAX_STATE_BYTES (8u * 1024u * 1024u)

struct VcspcVmDiagnostics {
    uint32_t size;
    uint32_t active;
    uint32_t failed;
    uint32_t entry;
    uint32_t main_bytes;
    uint32_t mission_bytes;
    uint32_t mission_count;
    uint32_t passes;
    uint32_t commands;
    uint32_t threads;
    uint32_t instruction_ip;
    uint32_t opcode;
    uint32_t fault;
    char message[512];
};

typedef const char* (VCSPC_VM_CALL *VcspcVmHostErrorProbe)();

struct VcspcVmApi {
    uint32_t size;
    uint32_t version;
    uint32_t host_size;
    uint32_t host_fingerprint;

    // The caller owns the callback functions for the entire open session.
    // The bridge copies the table and opens/closes its own diagnostic stream.
    int (VCSPC_VM_CALL *open)(const char* main_scm, const char* log_path,
                             const ReVcsScriptHost* host);
    void (VCSPC_VM_CALL *close)();
    int (VCSPC_VM_CALL *tick)(uint32_t milliseconds, float time_step);
    int (VCSPC_VM_CALL *is_on_mission)();

    // A null data pointer queries the required byte count. A state image is
    // valid only with its matching host-world save and restored entity pools.
    uint32_t (VCSPC_VM_CALL *save)(uint8_t* data, uint32_t capacity,
                                  int safehouse_menu);
    int (VCSPC_VM_CALL *validate)(const uint8_t* data, uint32_t bytes);
    int (VCSPC_VM_CALL *restore)(const uint8_t* data, uint32_t bytes);
    int (VCSPC_VM_CALL *diagnostics)(VcspcVmDiagnostics* output);

    // Configure before open. The message belongs to the host and is copied
    // immediately. The bridge raises and catches errors inside its own CRT.
    void (VCSPC_VM_CALL *set_host_error_probe)(VcspcVmHostErrorProbe probe);

    // Byte offsets in the VM-owned globals, for long-lived native HUD mirrors.
    int (VCSPC_VM_CALL *read_global)(uint32_t offset, int32_t* value);
    int (VCSPC_VM_CALL *write_global)(uint32_t offset, int32_t value);
};

typedef const VcspcVmApi* (VCSPC_VM_CALL *VcspcVmGetApiFn)(
    uint32_t version, uint32_t host_size, uint32_t host_fingerprint);

// The Windows export definition supplies this undecorated name on x86.
extern "C" const VcspcVmApi* VCSPC_VM_CALL VcspcVmGetApi(
    uint32_t version, uint32_t host_size, uint32_t host_fingerprint);

#endif
