#ifndef VCSPC_SCM_SA_COMMAND_H
#define VCSPC_SCM_SA_COMMAND_H

#include <initializer_list>

struct VcsScmSaArg {
    enum Kind { Integer, Real, TextLabel, IntegerOutput, RealOutput, IntegerReference };
    Kind kind;
    union {
        int integer;
        float real;
        const char* text;
        int* integerOutput;
        float* realOutput;
    } value;

    static VcsScmSaArg Int(int value);
    static VcsScmSaArg Float(float value);
    static VcsScmSaArg RawFloat(int bits);
    static VcsScmSaArg Text(const char* value);
    static VcsScmSaArg Out(int* value);
    static VcsScmSaArg Out(float* value);
    static VcsScmSaArg InOut(int* value);
};

// Explicitly approved immediate SA handlers. Signatures use i/f/s for inputs,
// I/F for outputs and R for an initialized, call-local integer reference.
struct VcsScmSaContract {
    unsigned short opcode;
    const char* signature;
    bool conditional;
    const char* name;
};
const VcsScmSaContract* VcsScmSaFindContract(unsigned short opcode);

// The return value reports dispatch success. Conditional results are written
// separately, without applying the VCS VM's NOT/AND/OR state.
bool VcsScmSaCall(unsigned short opcode, std::initializer_list<VcsScmSaArg> arguments,
                 bool* compare = nullptr, bool missionCleanup = true);

// The first fault remains latched until the next session. A null pointer means
// that no gateway fault has occurred. Later native commands are blocked.
const char* VcsScmSaError();
bool VcsScmSaReject(const char* message);
void VcsScmSaReset();
bool VcsScmSaVerifyNative();

#endif
