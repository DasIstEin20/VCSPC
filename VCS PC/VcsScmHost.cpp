#include "StdAfx.h"
#include "VcsScmHost.h"
#include "VcsScmHostInternal.h"
#include "VcsScmHostActors.h"
#include "VcsScmHostSaGarages.h"
#include "VcsScmHostSaRadar.h"
#include "VcsScmHostSaPopulation.h"
#include "VcsScmSaPresentation.h"
#include "VcsScmSaHud.h"
#include "VcsScmSaStatistics.h"
#include "ReVcsScriptVm.h"

void VcsScmBuildHost(ReVcsScriptHost& host)
{
    // Empty entries retain the VM's explicit unsupported-operation boundary.
    memset(&host, 0, sizeof(host));
    VcsScmBindModels(host);
    VcsScmBindEnvironment(host);
    VcsScmBindCamera(host);
    VcsScmBindPlayer(host);
    VcsScmBindActors(host);
    VcsScmBindSaPeds(host);
    VcsScmBindSaCombat(host);
    VcsScmBindSaVehicles(host);
    VcsScmBindSaVehicleState(host);
    VcsScmBindSaObjects(host);
    VcsScmBindSaTasks(host);
    VcsScmBindSaSpatial(host);
    VcsScmBindSaWorld(host);
    VcsScmBindSaFires(host);
    VcsScmBindSaGarages(host);
    VcsScmBindSaRadar(host);
    VcsScmBindSaPopulation(host);
    VcsScmBindSaPresentation(host);
    VcsScmBindSaSystem(host);
    VcsScmBindSaStatistics(host);
    VcsScmBindSaHud(host);
}

void VcsScmResetHost()
{
    VcsScmClearModelMap();
    VcsScmResetEnvironment();
    VcsScmResetHudState();
    VcsScmResetSaGarages();
    VcsScmResetStatisticsState();
    VcsScmResetSaRadar();
    VcsScmResetSaPopulation();
}
