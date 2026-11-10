# Experimental PS2 MAIN.SCM support

This optional integration brings the custom reVCS SCM virtual machine into VCSPC, which uses the GTA: San Andreas engine. It is designed to read the original *Grand Theft Auto: Vice City Stories* PS2 `MAIN.SCM` without translating it into SA scripts. The VM handles script bytecode, threads, variables and mission scheduling; a VCSPC host adapter translates supported operations to GTA SA engine calls and selected VCS-specific hooks.

**Experimental:** This is an incomplete port. Some script operations and game systems are not yet supported, and a full Windows build and campaign playthrough have not been validated.

## Build

The host is compiled for Win32 with the original VCSPC dependencies (including the v140 toolset, Boost and DirectX SDK). The VM DLL needs CMake, Python 3, a recent MSVC toolchain and a complete reVCS source checkout. The integration is based on VCSPC `eafa91e0` and reVCS `df799eb6` (`Bullet3Physics`).

```powershell
cmake -S scm-vm -B build-scm -G "Visual Studio 17 2022" -A Win32 -DREVCS_SOURCE_DIR="C:/path/to/reVCS"
cmake --build build-scm --config Release
msbuild "VCS PC.sln" /m /p:Configuration=Release /p:Platform=Win32 /p:ReVcsSourceDir="C:\\path\\to\\reVCS"
```

Place `VCSPC.ScmVm.dll` beside the rebuilt `VCSPC.dll`. In `VCSPC.ini`, enable the feature:

```ini
[ScriptVM]
enabled=1
path=data\script\vcs_ps2\main.scm
dll=VCSPC.ScmVm.dll
modelMap=data\script\vcs_ps2\models.tsv
log=vcspc-scm-vm.log
```

Supply your own original PS2 `MAIN.SCM`, VCS game assets and `models.tsv` mapping VCS source model IDs to installed native IDE model names (e.g. `0 player`). The original game data is not included.
