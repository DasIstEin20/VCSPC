#pragma once

struct ReVcsScriptHost;

void VcsScmBindModels(ReVcsScriptHost& host);
void VcsScmBindEnvironment(ReVcsScriptHost& host);
void VcsScmBindCamera(ReVcsScriptHost& host);
void VcsScmBindPlayer(ReVcsScriptHost& host);
void VcsScmClearModelMap();
void VcsScmResetEnvironment();

void VcsScmBindSaPeds(ReVcsScriptHost& host);
void VcsScmBindSaCombat(ReVcsScriptHost& host);
void VcsScmBindSaVehicles(ReVcsScriptHost& host);
void VcsScmBindSaVehicleState(ReVcsScriptHost& host);
void VcsScmBindSaObjects(ReVcsScriptHost& host);
void VcsScmBindSaTasks(ReVcsScriptHost& host);
void VcsScmBindSaSpatial(ReVcsScriptHost& host);
void VcsScmBindSaWorld(ReVcsScriptHost& host);
void VcsScmBindSaFires(ReVcsScriptHost& host);
