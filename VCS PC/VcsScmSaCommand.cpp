#include "StdAfx.h"
#include "Script.h"
#include "VcsScmSaCommand.h"

VcsScmSaArg VcsScmSaArg::Int(int value)
{
    VcsScmSaArg argument = {};
    argument.kind = Integer;
    argument.value.integer = value;
    return argument;
}

VcsScmSaArg VcsScmSaArg::Float(float value)
{
    VcsScmSaArg argument = {};
    argument.kind = Real;
    argument.value.real = value;
    return argument;
}

VcsScmSaArg VcsScmSaArg::RawFloat(int bits)
{
    VcsScmSaArg argument = {};
    argument.kind = Real;
    static_assert(sizeof(bits) == sizeof(argument.value.real), "SCM float operand size changed");
    memcpy(&argument.value.real, &bits, sizeof(bits));
    return argument;
}

VcsScmSaArg VcsScmSaArg::Text(const char* value)
{
    VcsScmSaArg argument = {};
    argument.kind = TextLabel;
    argument.value.text = value;
    return argument;
}

VcsScmSaArg VcsScmSaArg::Out(int* value)
{
    VcsScmSaArg argument = {};
    argument.kind = IntegerOutput;
    argument.value.integerOutput = value;
    return argument;
}

VcsScmSaArg VcsScmSaArg::Out(float* value)
{
    VcsScmSaArg argument = {};
    argument.kind = RealOutput;
    argument.value.realOutput = value;
    return argument;
}

VcsScmSaArg VcsScmSaArg::InOut(int* value)
{
    VcsScmSaArg argument = {};
    argument.kind = IntegerReference;
    argument.value.integerOutput = value;
    return argument;
}

namespace {

char gatewayError[256];
bool gatewayActive;
bool nativeVerified;

// Reviewed gta-vcs.exe, SHA256
// deb7ba9c5cc47805b5e1344f6ea32fefa02ba6183304fcef10b15898b0ba1fb1.
// The instruction at 0x469FEB indexes these original SA command groups.
const unsigned int nativeGroups[] = {
    0x00465E60u, 0x00466DE0u, 0x00469390u, 0x0047C100u, 0x0047D210u,
    0x0047E090u, 0x0047F370u, 0x0047FA30u, 0x00481300u, 0x00483BD0u,
    0x00489500u, 0x0048A320u, 0x0048B590u, 0x0048CDD0u, 0x0048EAA0u,
    0x00490DB0u, 0x00493FE0u, 0x00496E00u, 0x0046D050u, 0x0046B460u,
    0x00472310u, 0x00470A90u, 0x00474900u, 0x004762D0u, 0x00478000u,
    0x0047A760u, 0x00479DA0u
};

bool Fail(unsigned short opcode, const char* reason)
{
    if (!gatewayError[0]) {
        _snprintf_s(gatewayError, sizeof(gatewayError), _TRUNCATE,
            "SA opcode 0x%04X rejected, %s", opcode, reason);
        LogToFile("SCM VM: %s", gatewayError);
    }
    return false;
}

bool RequiresPersistentScript(unsigned short opcode)
{
    // Pinned gta-reversed eScriptCommands.h. These instructions own script
    // control flow, thread state or retained variable references. They cannot
    // execute against the temporary native context used by this gateway.
    if (opcode < 0x0052 && opcode != 0x0003)
        return true;
    switch (opcode) {
    case 0x00C5: case 0x00C6: // RETURN_TRUE / RETURN_FALSE
    case 0x00CD: case 0x00CE: // REPEAT / ENDREPEAT
    case 0x00D6: case 0x00D8: // ANDOR / MISSION_HAS_FINISHED
    case 0x014E: case 0x014F: case 0x0150: case 0x0151:
    case 0x0180: case 0x0181: case 0x0182:
    case 0x024F: case 0x03A1: case 0x03BC: // Render IDs derived from temporary script IP
    case 0x02A1: case 0x02CD: // MESSAGE_WAIT / GOSUB_FILE
    case 0x0396: case 0x03A4:
    case 0x03C3: case 0x03C4:
    case 0x0416: case 0x0417: case 0x0459:
    case 0x04F6: case 0x04F7: case 0x0515: case 0x059C:
    case 0x0661: case 0x0700: case 0x0701: case 0x0707:
    case 0x07B5: case 0x07B6: case 0x07B7: case 0x07B8:
    case 0x07D3: case 0x07D4: case 0x07F4:
    case 0x086D: case 0x0871: case 0x0872: case 0x0884:
    case 0x08AB: case 0x090F: case 0x0910: case 0x0911:
    case 0x0913: case 0x0914: case 0x0926: case 0x0927:
    case 0x0928: case 0x0929: case 0x093C:
        return true;
    default:
        return false;
    }
}

char Signature(const VcsScmSaArg& argument)
{
    switch (argument.kind) {
    case VcsScmSaArg::Integer: return 'i';
    case VcsScmSaArg::Real: return 'f';
    case VcsScmSaArg::TextLabel: return 's';
    case VcsScmSaArg::IntegerOutput: return 'I';
    case VcsScmSaArg::RealOutput: return 'F';
    case VcsScmSaArg::IntegerReference: return 'R';
    default: return '\0';
    }
}

class ParameterScratch {
    SCRIPT_VAR* original;
    SCRIPT_VAR saved[32];

public:
    ParameterScratch() : original(scriptParams)
    {
        memcpy(saved, original, sizeof(saved));
        memset(original, 0, sizeof(saved));
        gatewayActive = true;
    }

    ~ParameterScratch()
    {
        memcpy(original, saved, sizeof(saved));
        scriptParams = original;
        gatewayActive = false;
    }
};

} // namespace

// Friendship grants access to the existing native layout without extending
// CRunningScript. Its stock Init() indexes ScriptsArray and must never run on
// this call-local object.
class VcsScmSaGateway {
    CRunningScript script;
    unsigned char operands[512];
    unsigned int used;
    unsigned short outputCount;

    bool Append(unsigned char tag, const void* value, unsigned int bytes)
    {
        if (used + 1u + bytes > sizeof(operands))
            return false;
        operands[used++] = tag;
        memcpy(operands + used, value, bytes);
        used += bytes;
        return true;
    }

    bool Encode(unsigned short opcode, const VcsScmSaArg& argument)
    {
        switch (argument.kind) {
        case VcsScmSaArg::Integer:
            return Append(imm32, &argument.value.integer, 4) || Fail(opcode, "operand buffer is full");
        case VcsScmSaArg::Real: {
            unsigned int bits;
            memcpy(&bits, &argument.value.real, sizeof(bits));
            if ((bits & 0x7F800000u) == 0x7F800000u)
                return Fail(opcode, "non-finite float input");
            return Append(imm32f, &bits, 4) || Fail(opcode, "operand buffer is full");
        }
        case VcsScmSaArg::TextLabel: {
            if (!argument.value.text)
                return Fail(opcode, "null text input");
            unsigned int length = 0;
            while (length <= 16 && argument.value.text[length])
                ++length;
            if (length > 16)
                return Fail(opcode, "text exceeds the 16-byte SA operand limit");
            char text[16] = {};
            memcpy(text, argument.value.text, length);
            return Append(length <= 8 ? sstring : lstring, text, length <= 8 ? 8 : 16) ||
                Fail(opcode, "operand buffer is full");
        }
        case VcsScmSaArg::IntegerOutput:
        case VcsScmSaArg::IntegerReference:
        case VcsScmSaArg::RealOutput:
            if ((argument.kind == VcsScmSaArg::RealOutput && !argument.value.realOutput) ||
                (argument.kind != VcsScmSaArg::RealOutput && !argument.value.integerOutput))
                return Fail(opcode, "null output reference");
            if (outputCount >= sizeof(script.LocalVar) / sizeof(script.LocalVar[0]))
                return Fail(opcode, "native local-variable capacity exceeded");
            if (argument.kind == VcsScmSaArg::IntegerReference)
                script.LocalVar[outputCount].iParam = *argument.value.integerOutput;
            if (!Append(localVar, &outputCount, sizeof(outputCount)))
                return Fail(opcode, "operand buffer is full");
            ++outputCount;
            return true;
        default:
            return Fail(opcode, "unknown argument kind");
        }
    }

public:
    VcsScmSaGateway() : used(0), outputCount(0)
    {
        memset(&script, 0, sizeof(script));
        memset(operands, 0, sizeof(operands));
        memcpy(script.Name, "RVCSGAT", 7);
        script.BaseIP = operands;
        script.CurrentIP = operands;
        script.bIsActive = true;
        script.extrnAttachType = -1;
    }

    bool Call(unsigned short opcode, std::initializer_list<VcsScmSaArg> arguments,
              bool* compare, bool missionCleanup)
    {
        if (gatewayError[0])
            return false;
        if (gatewayActive)
            return Fail(opcode, "reentrant native dispatch");
        if (!VcsScmSaVerifyNative())
            return false;
        // The original SA table contains 27 groups of 100 commands. Its last
        // vanilla command is DO_DEBUG_STUFF, 0x0A4E. NOT belongs to the VM.
        if (opcode & 0x8000)
            return Fail(opcode, "NOT must be evaluated by the VCS VM");
        if (opcode > 0x0A4E)
            return Fail(opcode, "command is outside the original SA opcode range");
        if (RequiresPersistentScript(opcode))
            return Fail(opcode, "command requires a persistent native script");
        const VcsScmSaContract* contract = VcsScmSaFindContract(opcode);
        if (!contract || contract->opcode != opcode || !contract->signature)
            return Fail(opcode, "command has no approved SA contract");
        if (contract->conditional != (compare != nullptr))
            return Fail(opcode, "conditional-result contract mismatch");
        if (arguments.size() > 32 || strlen(contract->signature) != arguments.size())
            return Fail(opcode, "argument count mismatch");
        unsigned int argumentIndex = 0;
        for (const VcsScmSaArg& argument : arguments) {
            if (Signature(argument) != contract->signature[argumentIndex++])
                return Fail(opcode, "argument type mismatch");
            if (!Encode(opcode, argument))
                return false;
        }
        if (!scriptParams)
            return Fail(opcode, "native parameter scratch is unavailable");
        typedef int8 (__thiscall *CommandFunction)(CRunningScript*, int32);
        CommandFunction* commands = *reinterpret_cast<CommandFunction**>(0x469FEE);
        if (reinterpret_cast<unsigned int>(commands) != 0x8A6168u ||
            reinterpret_cast<unsigned int>(commands[opcode / 100]) != nativeGroups[opcode / 100])
            return Fail(opcode, "native command group differs from the reviewed executable");
        script.bUseMissionCleanup = missionCleanup;
        ParameterScratch scratch;
        const int8 result = commands[opcode / 100](&script, opcode);
        if (gatewayError[0])
            return false;
        if (result != 0 || script.CurrentIP != operands + used || script.SP != 0 ||
            script.WakeTime != 0 || script.SceneSkipIP || script.Previous || script.Next ||
            !script.bIsActive || script.bIsMission || script.bIsExternal ||
            script.extrnAttachType != -1 || script.NotFlag || script.LogicalOp != 0)
            return Fail(opcode, "native handler violated its immediate-call contract");
        unsigned short output = 0;
        for (const VcsScmSaArg& argument : arguments) {
            if (argument.kind == VcsScmSaArg::IntegerOutput || argument.kind == VcsScmSaArg::IntegerReference)
                *argument.value.integerOutput = script.LocalVar[output++].iParam;
            else if (argument.kind == VcsScmSaArg::RealOutput)
                *argument.value.realOutput = script.LocalVar[output++].fParam;
        }
        if (compare)
            *compare = script.bCondResult;
        return true;
    }
};

bool VcsScmSaCall(unsigned short opcode, std::initializer_list<VcsScmSaArg> arguments,
                 bool* compare, bool missionCleanup)
{
    VcsScmSaGateway gateway;
    return gateway.Call(opcode, arguments, compare, missionCleanup);
}

const char* VcsScmSaError()
{
    return gatewayError[0] ? gatewayError : nullptr;
}

bool VcsScmSaReject(const char* message)
{
    if (!gatewayError[0]) {
        if (!message || !message[0])
            message = "native adapter rejected the operation";
        _snprintf_s(gatewayError, sizeof(gatewayError), _TRUNCATE, "%s", message);
        LogToFile("SCM VM: %s", gatewayError);
    }
    return false;
}

void VcsScmSaReset()
{
    if (gatewayActive) {
        Fail(0, "reset during native dispatch");
        return;
    }
    gatewayError[0] = '\0';
    nativeVerified = false;
}

bool VcsScmSaVerifyNative()
{
    if (gatewayError[0])
        return false;
    if (nativeVerified)
        return true;
    const HMODULE module = GetModuleHandleA(nullptr);
    if (reinterpret_cast<unsigned int>(module) != 0x400000u)
        return Fail(0, "executable image base differs from the reviewed x86 build");
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew != 0x80)
        return Fail(0, "executable DOS header differs from the reviewed build");
    const IMAGE_NT_HEADERS32* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(0x400080);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->FileHeader.TimeDateStamp != 0x427101CAu ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->OptionalHeader.ImageBase != 0x400000u || pe->OptionalHeader.SizeOfImage != 0x8D5000u)
        return Fail(0, "executable PE identity differs from the reviewed x86 build");
    const unsigned char dispatch[] = {0x8B, 0x14, 0x95, 0x68, 0x61, 0x8A, 0x00};
    if (memcmp(reinterpret_cast<const void*>(0x469FEB), dispatch, sizeof(dispatch)) != 0 ||
        memcmp(reinterpret_cast<const void*>(0x8A6168), nativeGroups, sizeof(nativeGroups)) != 0)
        return Fail(0, "original SA dispatch table differs from the reviewed executable");
    nativeVerified = true;
    return true;
}
