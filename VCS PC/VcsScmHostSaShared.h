#pragma once

class CPed;
class CVehicle;
class CObject;

CPed* VcsScmSaPed(int handle);
CVehicle* VcsScmSaVehicle(int handle);
CObject* VcsScmSaObject(int handle);
int VcsScmSaObjectPoolSize();
CObject* VcsScmSaObjectAtIndex(int index, int* handle);

bool VcsScmSaPedExists(int handle);
bool VcsScmSaVehicleExists(int handle);
bool VcsScmSaObjectExists(int handle);
bool VcsScmSaModelLoaded(int model);
bool VcsScmSaModelKind(int model, unsigned char kind);
bool VcsScmSaVector(const float* xyz);
bool VcsScmSaGroundPosition(const float* xyz, bool ground, float* position);
bool VcsScmSaMissionEntity(int handle, unsigned char type, bool mission);
bool VcsScmSaDrawLocateMarker(unsigned int id, const float* center, const float* radius, bool three, float actorZ);

// VCS source enums are translated independently from native model IDs.
int VcsScmSaPedType(int source);
int VcsScmSaWeaponType(int source);
int VcsScmSaSourceWeapon(int nativeType);
int VcsScmSaDamageType(int source);
int VcsScmSaWeaponSlot(int sourceSlot);
