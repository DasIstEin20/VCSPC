#ifndef VCSPC_SCM_RUNTIME_H
#define VCSPC_SCM_RUNTIME_H

bool VcsScmEnabled();
bool VcsScmReady();
void VcsScmInitialise();
void VcsScmStart();
void VcsScmObserveFrame();
void VcsScmProcess();
bool VcsScmIsPlayerOnMission();
bool VcsScmReadGlobal(unsigned int offset, int* value);
bool VcsScmWriteGlobal(unsigned int offset, int value);
void VcsScmSave();
void VcsScmLoad();
void VcsScmShutdown();
void VcsScmInstallHooks();

#endif
