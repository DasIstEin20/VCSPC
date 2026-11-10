#include "StdAfx.h"
#include "VcsScmSaPresentation.h"
#include "VcsScmSaCommand.h"
#include "ReVcsScriptVm.h"
#include "Text.h"
#include "Hud.h"
#include "UserDisplay.h"
#include "Pad.h"
#include "Font.h"
#include "Sprite.h"
#include "Rs.h"

#include <stdint.h>

// SA 1.0 US TextCommands, DrawCommands and Messages, gta-reversed afbc26d7.
// VCS operands and queue policy follow reVCS df799eb6 NativeScriptMessages
// and NativeScriptDisplay. Command identifiers below are SA identifiers.
namespace {
using Arg = VcsScmSaArg;

// Fixed x86 layouts from SA tScriptText and tMessage. Pointer fields are
// uint32_t because these structures describe engine storage, not C++ owners.
struct NativeTextRow {
    float scaleX, scaleY;
    uint32_t colour;
    uint8_t justify, centred, background, backgroundTextOnly;
    float wrapX, centreSize;
    uint32_t backgroundColour;
    uint8_t proportional;
    uint8_t shadowColour[4];
    int8_t shadow, edge;
    uint8_t beforeFade, rightJustify;
    uint8_t padding[3];
    int font;
    float x, y;
    char key[8];
    int firstNumber, secondNumber;
};
static_assert(sizeof(NativeTextRow) == 0x44, "SA script text layout");

struct NativeMessage {
    uint32_t text;
    uint16_t flags, padding;
    uint32_t duration, started;
    int numbers[6];
    uint32_t string;
    uint8_t previousBrief, tailPadding[3];
};
static_assert(sizeof(NativeMessage) == 48, "SA message layout");

unsigned short& textCount = *reinterpret_cast<unsigned short*>(0xA44B68);
unsigned char& textMode = *reinterpret_cast<unsigned char*>(0xA44B67);
NativeTextRow* const textRows = reinterpret_cast<NativeTextRow*>(0xA913E8);
NativeMessage* const briefs = reinterpret_cast<NativeMessage*>(0xC1A7F0);
NativeMessage* const bigMessages = reinterpret_cast<NativeMessage*>(0xC1A970);

const char* Text(const char* key)
{
    return key && *key && strlen(key) <= 8 ? TheText.Get(key) : nullptr;
}

int BigDuration(int duration, unsigned short style)
{
    // Original VCS CMessages fixes these three presentation styles to 4900ms.
    return style < 2 || style == 6 ? 4900 : duration;
}

bool Print(const char* key, int duration, unsigned short flags)
{
    return Text(key) && VcsScmSaCall(0x00BB, {Arg::Text(key), Arg::Int(duration), Arg::Int(flags)});
}

bool PrintNow(const char* key, int duration, unsigned short flags)
{
    return Text(key) && VcsScmSaCall(0x00BC, {Arg::Text(key), Arg::Int(duration), Arg::Int(flags)});
}

bool PrintBig(const char* key, int duration, unsigned short style)
{
    return style < 7 && Text(key) && VcsScmSaCall(0x00BA,
        {Arg::Text(key), Arg::Int(BigDuration(duration, style)), Arg::Int(style + 1)});
}

bool PrintBigQueued(const char* key, int duration, unsigned short style)
{
    return style < 7 && Text(key) && VcsScmSaCall(0x0217,
        {Arg::Text(key), Arg::Int(BigDuration(duration, style)), Arg::Int(style + 1)});
}

bool PrintNumberBig(const char* key, int number, int duration, short style)
{
    return style >= 0 && style < 7 && Text(key) && VcsScmSaCall(0x01E3,
        {Arg::Text(key), Arg::Int(number), Arg::Int(BigDuration(duration, style)), Arg::Int(style + 1)});
}

bool PrintTwoNumbersBig(const char* key, int first, int second, int duration, unsigned short style)
{
    return style < 7 && Text(key) && VcsScmSaCall(0x036D,
        {Arg::Text(key), Arg::Int(first), Arg::Int(second),
         Arg::Int(BigDuration(duration, style)), Arg::Int(style + 1)});
}

bool PrintManyNumbersBig(unsigned short opcode, const char* key, const int* numbers,
                         int duration, unsigned short style)
{
    const char* text = Text(key);
    if (!text || !numbers || style >= 7 || (opcode != 0x0144 && opcode != 0x021F))
        return false;
    // SA lacks stock PRINT_WITH_NUMBER_BIG_Q and PRINT_WITH_3_NUMBERS_BIG.
    // Its existing CMessages entry points own the same six-number queues.
    typedef void (__cdecl* AddBigNumbers)(const char*, unsigned int, unsigned short,
                                         int, int, int, int, int, int);
    const uintptr_t address = opcode == 0x0144 ? 0x69E6E0 : 0x69E5F0;
    reinterpret_cast<AddBigNumbers>(address)(text, BigDuration(duration, style), style,
        numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5]);
    return true;
}

void AddBrief(const char* text, const int* numbers, int duration, unsigned short flags,
              bool immediate, bool history)
{
    typedef void (__cdecl* AddNumbers)(const char*, unsigned int, unsigned short,
                                      int, int, int, int, int, int, bool);
    const uintptr_t address = immediate ? 0x69E4E0 : 0x69E360;
    reinterpret_cast<AddNumbers>(address)(text, duration, flags,
        numbers[0], numbers[1], numbers[2], numbers[3], numbers[4], numbers[5], history);
}

void AddSoon(const char* text, const int* numbers, int duration, unsigned short flags)
{
    // VCS inserts immediately behind the current message and drops the last
    // queued message when full. Let SA create the record before moving it.
    unsigned int slot = 0;
    while (slot < 8 && briefs[slot].text)
        ++slot;
    if (slot == 8) {
        slot = 7;
        briefs[slot].text = 0;
    }
    AddBrief(text, numbers, duration, flags, false, true);
    if (slot > 1) {
        const NativeMessage added = briefs[slot];
        memmove(briefs + 2, briefs + 1, (slot - 1) * sizeof(NativeMessage));
        briefs[1] = added;
    }
}

bool PrintSoon(const char* key, int duration, unsigned short flags)
{
    const char* text = Text(key);
    if (!text)
        return false;
    const int numbers[6] = {-1, -1, -1, -1, -1, -1};
    AddSoon(text, numbers, duration, flags);
    return true;
}

bool PrintNumbersBriefMany(unsigned short opcode, const char* key, const int* numbers,
                           int duration, unsigned short flags)
{
    const char* text = Text(key);
    if (!text || !numbers)
        return false;
    switch (opcode) {
    case 0x011E: case 0x01D8: case 0x01DB: case 0x01DD: case 0x01DE:
        AddBrief(text, numbers, duration, flags, false, true);
        return true;
    case 0x011F: case 0x01D9: case 0x01DC:
        AddBrief(text, numbers, duration, flags, true, true);
        return true;
    case 0x03ED: case 0x03EC: case 0x03F4:
        AddBrief(text, numbers, duration, flags, true, false);
        return true;
    case 0x0120: case 0x01DA:
        AddSoon(text, numbers, duration, flags);
        return true;
    default:
        return false;
    }
}

bool PrintNumbersBrief(unsigned short opcode, const char* key, int first, int second,
                      int third, int duration, unsigned short flags)
{
    const int numbers[6] = {first, second, third, -1, -1, -1};
    return PrintNumbersBriefMany(opcode, key, numbers, duration, flags);
}

bool PrintStringNow(const char* outer, const char* inner, int duration, unsigned short flags)
{
    return Text(outer) && Text(inner) && VcsScmSaCall(0x0384,
        {Arg::Text(outer), Arg::Text(inner), Arg::Int(duration), Arg::Int(flags)});
}

bool ClearThisPrint(const char* key)
{
    return key && VcsScmSaCall(0x03D5, {Arg::Text(key)});
}

bool ClearThisBigPrint(const char* key)
{
    return Text(key) && VcsScmSaCall(0x03D6, {Arg::Text(key)});
}

void ClearPrints() { VcsScmSaCall(0x00BE, {}); }
void ClearHelp() { VcsScmSaCall(0x03E6, {}); }

void ClearMessageScope(bool big)
{
    if (big) {
        for (unsigned int slot = 0; slot < 7 * 4; ++slot) {
            bigMessages[slot].text = 0;
            bigMessages[slot].string = 0;
        }
        // Source retains the HUD's style2 buffer and all animation timers.
        char (*displayed)[128] = reinterpret_cast<char (*)[128]>(0xBAACC0);
        for (unsigned int style = 0; style < 7; ++style)
            if (style != 2) memset(displayed[style], 0, 128);
    } else {
        VcsScmSaCall(0x03EB, {});
        CHud::ClearScmSmallMessages();
    }
}

bool SkipMessageWait()
{
    CPad* pad = CPad::GetPad(0);
    if (!pad->NewState.SQUARE || pad->OldState.SQUARE) return false;
    for (unsigned int style = 0; style < 7; ++style)
        if (bigMessages[style * 4].text) bigMessages[style * 4].started = 0;
    return true;
}

bool PagerAddNumber(const char* key, int number, unsigned short speed,
                     unsigned short priority, unsigned short)
{
    const char* text = Text(key);
    // VCS carries the final word in its queue but does not consume it.
    return text && CUserDisplay::Pager.AddMessageWithNumber(const_cast<char*>(text), number, speed, priority);
}

bool PagerAdd(const char* key, unsigned short speed, unsigned short priority, unsigned short extra)
{
    return PagerAddNumber(key, -1, speed, priority, extra);
}

bool PagerOn() { return CHud::IsPagerOn(); }
void PagerClear() { CUserDisplay::Pager.ClearMessages(); }

bool HelpMessage(const char* key, bool forever, bool always, bool addToBrief)
{
    const char* text = Text(key);
    if (key && !text)
        return false;
    if (text && addToBrief) {
        reinterpret_cast<void (__cdecl*)(const char*, int, int, int, int, int, int, const char*)>(
            0x69DD50)(text, -1, -1, -1, -1, -1, -1, nullptr);
    }
    CHud::SetScmHelpMessage(text, forever, always);
    return true;
}

bool PrintHelp(const char* key) { return HelpMessage(key, false, false, true); }
bool PrintHelpAlways(const char* key, bool forever) { return HelpMessage(key, forever, true, true); }

bool TextLength(const char* key, int* length)
{
    const char* text = Text(key);
    if (!text || !length)
        return false;
    *length = static_cast<int>(strlen(text));
    return true;
}

bool MissionTextLoad(const char* name)
{
    return name && *name && strlen(name) <= 8 && VcsScmSaCall(0x054C, {Arg::Text(name)});
}

bool AreaName(const char* key, bool visible)
{
    const char* text = visible ? Text(key) : nullptr;
    if (visible && !text)
        return false;
    CHud::SetZoneName(const_cast<char*>(text), true);
    return true;
}

void HudFlash(short item) { VcsScmSaCall(0x03E7, {Arg::Int(item)}); }

void TextCommandMode(unsigned char mode)
{
    // Both hosts use 0=disabled, 1=clear next pass, 2=script-owned drawing.
    textMode = mode;
}

bool TextStyle(unsigned short opcode, const int* values)
{
    if (!values || textCount >= 96)
        return false;
    switch (opcode) {
    case 0x0204: {
        float scale[2];
        memcpy(scale, values, sizeof(scale));
        if (!(scale[0] > 0 && scale[0] <= 100 && scale[1] > 0 && scale[1] <= 100))
            return false;
        return VcsScmSaCall(0x033F, {Arg::Float(scale[0]), Arg::Float(scale[1])});
    }
    case 0x0205:
        return VcsScmSaCall(0x0340, {Arg::Int(static_cast<unsigned char>(values[0])),
            Arg::Int(static_cast<unsigned char>(values[1])), Arg::Int(static_cast<unsigned char>(values[2])),
            Arg::Int(static_cast<unsigned char>(values[3]))});
    case 0x0206: return VcsScmSaCall(0x0341, {Arg::Int(values[0] != 0)});
    case 0x0207: return VcsScmSaCall(0x0342, {Arg::Int(values[0] != 0)});
    case 0x0208: {
        float width;
        memcpy(&width, values, sizeof(width));
        if (!(width >= 0 && width <= 10000))
            return false;
        textRows[textCount].wrapX = Min(width, 480.0f);
        return true;
    }
    case 0x0209: return VcsScmSaCall(0x0345, {Arg::Int(values[0] != 0)});
    case 0x020A: return VcsScmSaCall(0x0348, {Arg::Int(values[0] != 0)});
    case 0x0266: return VcsScmSaCall(0x03E4, {Arg::Int(values[0] != 0)});
    case 0x054A: {
        const unsigned char font = static_cast<unsigned char>(values[0]);
        if (font > 2)
            return false;
        // VCS bank/standard text uses VCSPC's Eurostile, headings use Rage.
        return VcsScmSaCall(0x0349, {Arg::Int(font == 2 ? FONT_RageItalic : FONT_Eurostile)});
    }
    default: return false;
    }
}

bool DisplayLine(float x, float y, const char* key, int first, int second, bool beforeFade, bool plain)
{
    if (!plain && textCount >= 20)
        return true;
    const char* text = Text(key);
    if (!text || !(x >= -10000 && x <= 10000 && y >= -10000 && y <= 10000))
        return false;
    // The source drops lines after twenty even though SA has 96 slots.
    if (textCount >= 20)
        return true;
    // The VCS plain-text commands cap the dictionary string at63 units.
    // Numeric commands use their independent formatter and keep the source text.
    if (plain && strlen(text) >= 64)
        const_cast<char*>(text)[63] = 0;
    NativeTextRow& row = textRows[textCount];
    row.beforeFade = beforeFade;
    row.wrapX = (x + row.wrapX) * (640.0f / 480.0f);
    row.centreSize = 640.0f;
    return VcsScmSaCall(0x045B, {Arg::Float(x * (640.0f / 480.0f)),
        Arg::Float(y * (448.0f / 272.0f)), Arg::Text(key), Arg::Int(first), Arg::Int(second)});
}

bool DisplayText(float x, float y, const char* key) { return DisplayLine(x, y, key, -1, -1, false, true); }
bool DisplayTextBeforeFade(float x, float y, const char* key) { return DisplayLine(x, y, key, -1, -1, true, true); }
bool DisplayTextNumber(float x, float y, const char* key, int first, int second)
{
    return DisplayLine(x, y, key, first, second, false, false);
}

bool ScreenCoordinates(const float* xyz, int* xy)
{
    if (!xyz || !xy || !RsGlobal.MaximumWidth || !RsGlobal.MaximumHeight)
        return false;
    for (unsigned int index = 0; index < 3; ++index)
        if (!(xyz[index] >= -100000 && xyz[index] <= 100000)) return false;
    const RwV3d position = {xyz[0], xyz[1], xyz[2]};
    RwV3d screen = {};
    float width = 0, height = 0;
    CSprite::CalcScreenCoors(position, &screen, &width, &height, false, false);
    if (screen.z <= 0) screen.x = screen.y = 0;
    const float values[] = {screen.x * (480.0f / RsGlobal.MaximumWidth),
                            screen.y * (272.0f / RsGlobal.MaximumHeight)};
    for (unsigned int index = 0; index < 2; ++index)
        xy[index] = values[index] >= -2147483648.0f && values[index] < 2147483648.0f ?
            static_cast<int>(values[index]) : (-2147483647 - 1);
    return true;
}

}

void VcsScmSaPresentationBeginPass()
{
    // This is the existing SA Process reset, performed only on actual VM
    // passes. Intermediate render frames retain the last submitted text.
    if (!textMode)
        return;
    NativeTextRow empty = {};
    empty.scaleX = 0.48f;
    empty.scaleY = 1.12f;
    empty.colour = 0xFFE1E1E1u;
    empty.wrapX = 480.0f;
    empty.centreSize = 640.0f;
    empty.backgroundColour = 0x80808080u;
    empty.proportional = 1;
    empty.shadowColour[3] = 255;
    empty.shadow = 2;
    empty.font = FONT_Eurostile;
    empty.firstNumber = empty.secondNumber = -1;
    for (unsigned int i = 0; i < 96; ++i)
        textRows[i] = empty;
    textCount = 0;
    // No presentation callback currently queues native script rectangles.
    *reinterpret_cast<unsigned short*>(0xA44B5C) = 0;
    memset(reinterpret_cast<void*>(0xA92D68), 0, 128 * 0x3C);
    if (textMode == 1)
        textMode = 0;
}

void VcsScmBindSaPresentation(ReVcsScriptHost& host)
{
    textMode = 2;
    VcsScmSaPresentationBeginPass();
    textMode = 0;
    host.print = Print;
    host.printNow = PrintNow;
    host.printSoon = PrintSoon;
    host.printBig = PrintBig;
    host.printBigQueued = PrintBigQueued;
    host.printNumberBig = PrintNumberBig;
    host.printTwoNumbersBig = PrintTwoNumbersBig;
    host.printManyNumbersBig = PrintManyNumbersBig;
    host.printNumbersBrief = PrintNumbersBrief;
    host.printNumbersBriefMany = PrintNumbersBriefMany;
    host.printStringNow = PrintStringNow;
    host.clearThisPrint = ClearThisPrint;
    host.clearThisBigPrint = ClearThisBigPrint;
    host.clearPrints = ClearPrints;
    host.clearHelp = ClearHelp;
    host.clearMessageScope = ClearMessageScope;
    host.skipMessageWait = SkipMessageWait;
    host.pagerAdd = PagerAdd;
    host.pagerAddNumber = PagerAddNumber;
    host.pagerOn = PagerOn;
    host.pagerClear = PagerClear;
    host.helpMessage = HelpMessage;
    host.printHelp = PrintHelp;
    host.printHelpAlways = PrintHelpAlways;
    host.textLength = TextLength;
    host.missionTextLoad = MissionTextLoad;
    host.areaName = AreaName;
    host.hudFlash = HudFlash;
    host.textCommandMode = TextCommandMode;
    host.textStyle = TextStyle;
    host.displayText = DisplayText;
    host.displayTextBeforeFade = DisplayTextBeforeFade;
    host.displayTextNumber = DisplayTextNumber;
    host.screenCoordinates = ScreenCoordinates;
}
