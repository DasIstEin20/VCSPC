#pragma once

struct ReVcsScriptHost;

void VcsScmBindSaPresentation(ReVcsScriptHost& host);
void VcsScmBindSaSystem(ReVcsScriptHost& host);
void VcsScmBindSaStatistics(ReVcsScriptHost& host);
void VcsScmSaPresentationBeginPass();
void VcsScmSaResetInputQueries();
bool VcsScmSaPauseDisabled();
void VcsScmSaSetPauseDisabled(bool disabled);
