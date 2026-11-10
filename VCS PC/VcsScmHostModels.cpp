#include "StdAfx.h"
#include "VcsScmHost.h"
#include "VcsScmHostInternal.h"
#include "ReVcsScriptVm.h"
#include "ModelInfo.h"
#include "Streaming.h"
#include <fstream>
#include <sstream>

namespace {
std::map<int, int> sourceModels;
std::map<int, int> nativeModels;

bool ValidModel(int model)
{
    return model >= 0 && model < 20000 && CModelInfo::GetModelInfo(static_cast<uint16>(model));
}

int ModelByName(const char* name)
{
    int model = -1;
    return name && name[0] && CModelInfo::GetModelInfo(name, &model) && ValidModel(model) ? model : -1;
}

int ModelBySource(unsigned int source)
{
    return source <= 65535 ? VcsScmModelBySource(static_cast<int>(source)) : -1;
}

bool RequestModel(int model, unsigned char sourceFlags, const char*, unsigned short)
{
    if (!ValidModel(model) || (sourceFlags != 0x16 && sourceFlags != 0x94))
        return false;

    // SA 1.0 US StreamingInfo.h. The two source ownership kinds are translated
    // explicitly, while SA's existing streaming worker performs the actual load.
    const int flags = sourceFlags == 0x16 ? 0x4 : 0x2;
    CStreaming::RequestModel(model, flags | 0x8);
    return true;
}

bool ModelLoaded(int model, bool* loaded)
{
    if (!ValidModel(model) || !loaded)
        return false;
    *loaded = CStreaming::ms_aInfoForModel[model].uLoadStatus == StreamingModelLoaded;
    return true;
}

bool ReleaseModel(int model, bool mission)
{
    if (!ValidModel(model))
        return false;
    if (mission)
        reinterpret_cast<void (__cdecl*)(int)>(0x409C90)(model);
    else {
        CStreaming::SetModelIsDeletable(model);
        CStreaming::SetModelTxdIsDeletable(model);
    }
    return true;
}

void LoadAllModels()
{
    CStreaming::LoadAllRequestedModels(false);
}

void StreamingSwitch(bool enabled)
{
    // SA 1.0 US CStreaming::ms_disableStreaming, read by its normal Update.
    *reinterpret_cast<bool*>(0x9654B0) = !enabled;
}

bool StreamingBusy()
{
    return *reinterpret_cast<int*>(0x8E4CB8) != 0 || *reinterpret_cast<bool*>(0x8E4A58);
}
}

bool VcsScmLoadModelMap(const char* path, FILE* log)
{
    if (!path || !log)
        return false;
    std::ifstream input(path);
    if (!input) {
        fprintf(log, "model_map_open_failed path=%s\n", path);
        return false;
    }

    std::map<int, int> candidateSources;
    std::map<int, int> candidateNatives;
    std::string line;
    unsigned int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::string::size_type comment = line.find('#');
        if (comment != std::string::npos)
            line.erase(comment);
        std::istringstream row(line);
        row >> std::ws;
        if (row.eof())
            continue;
        std::string sourceText, name, extra;
        if (line.size() > 256 || !(row >> sourceText >> name) || (row >> extra) ||
            sourceText.find_first_not_of("0123456789") != std::string::npos ||
            name.empty() || name.size() > 23 ||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos) {
            fprintf(log, "model_map_invalid_row line=%u\n", lineNumber);
            return false;
        }
        int source = -1;
        std::istringstream sourceValue(sourceText);
        if (!(sourceValue >> source) || !sourceValue.eof() || source < 0 || source > 65535) {
            fprintf(log, "model_map_invalid_source line=%u\n", lineNumber);
            return false;
        }
        const int model = ModelByName(name.c_str());
        if (model < 0 || candidateSources.count(source) || candidateNatives.count(model)) {
            fprintf(log, "model_map_unresolved_or_duplicate line=%u source=%d name=%s native=%d\n",
                lineNumber, source, name.c_str(), model);
            return false;
        }
        candidateSources[source] = model;
        candidateNatives[model] = source;
    }
    if (input.bad() || candidateSources.empty()) {
        fprintf(log, "model_map_empty_or_unreadable\n");
        return false;
    }

    // Commit both directions together. Failed parsing retains the prior map.
    sourceModels.swap(candidateSources);
    nativeModels.swap(candidateNatives);
    fprintf(log, "model_map_loaded entries=%u\n", static_cast<unsigned int>(sourceModels.size()));
    fflush(log);
    return true;
}

int VcsScmModelBySource(int source)
{
    const auto found = sourceModels.find(source);
    return found != sourceModels.end() && ValidModel(found->second) ? found->second : -1;
}

int VcsScmSourceByModel(int model)
{
    const auto found = nativeModels.find(model);
    return found != nativeModels.end() && ValidModel(model) ? found->second : -1;
}

void VcsScmClearModelMap()
{
    sourceModels.clear();
    nativeModels.clear();
}

void VcsScmBindModels(ReVcsScriptHost& host)
{
    host.modelByName = ModelByName;
    host.streamingModelBySource = ModelBySource;
    host.streamingModelSource = VcsScmSourceByModel;
    host.pickupModelBySource = ModelBySource;
    host.objectModelBySource = ModelBySource;
    host.streamingModelRequest = RequestModel;
    host.streamingModelLoaded = ModelLoaded;
    host.streamingModelRelease = ReleaseModel;
    host.loadAllModels = LoadAllModels;
    host.streamingSwitch = StreamingSwitch;
    host.streamingBusy = StreamingBusy;
}
