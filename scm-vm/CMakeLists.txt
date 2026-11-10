cmake_minimum_required(VERSION 3.20)
project(vcspc_scm_vm LANGUAGES C CXX)

set(REVCS_SOURCE_DIR "" CACHE PATH "reVCS checkout containing reVCS and vcs_vm_core")
set(REVCS_CORE "${REVCS_SOURCE_DIR}/reVCS/src/core")
set(REVCS_OWNED_VM "${REVCS_SOURCE_DIR}/reVCS/src/vcs_vm")
set(REVCS_VM "${REVCS_SOURCE_DIR}/vcs_vm_core")
if(NOT EXISTS "${REVCS_CORE}/ReVcsScriptVm.h" OR
   NOT EXISTS "${REVCS_OWNED_VM}/ReVcsMissionHost.cpp" OR
   NOT EXISTS "${REVCS_VM}/src/ScriptVmCore.cpp")
    message(FATAL_ERROR "REVCS_SOURCE_DIR must name the complete reVCS repository root")
endif()
file(SHA256 "${REVCS_CORE}/ReVcsScriptVm.h" HOST_SHA256)
if(NOT HOST_SHA256 STREQUAL "baea4470747017ddee9218b8a950e175710d70a694a9b21f3d8212b43dcbdb45")
    message(FATAL_ERROR "ReVcsScriptVm.h differs from the reviewed VCSPC host ABI")
endif()
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "VCSPC requires an x86 VM DLL. Configure with -A Win32")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory("${REVCS_SOURCE_DIR}/reVCS/vendor/zstd" "zstd")

set(GUARDS "${CMAKE_CURRENT_BINARY_DIR}/generated/HostGuards.inc")
add_custom_command(OUTPUT "${GUARDS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_host_guards.py"
            --header "${REVCS_CORE}/ReVcsScriptVm.h" --output "${GUARDS}"
    DEPENDS tools/generate_host_guards.py "${REVCS_CORE}/ReVcsScriptVm.h"
    VERBATIM)

file(GLOB SCRIPT_BINDINGS CONFIGURE_DEPENDS "${REVCS_CORE}/ReVcsScriptBindings*.cpp")
add_library(vcspc_scm_vm SHARED
    src/VmBridge.cpp "${GUARDS}"
    "${REVCS_VM}/src/ScriptVmCore.cpp"
    "${REVCS_OWNED_VM}/ReVcsMissionHost.cpp"
    "${REVCS_CORE}/ReVcsScriptVm.cpp"
    "${REVCS_CORE}/ReVcsScriptDiagnostics.cpp"
    "${REVCS_CORE}/ReVcsScriptDebugCatalog.cpp"
    "${REVCS_CORE}/ReVcsScriptStateCodec.cpp"
    "${REVCS_CORE}/ReVcsScriptMissionAssist.cpp"
    "${REVCS_CORE}/ReVcsAssetLayout.cpp"
    "${REVCS_CORE}/ReVcsArchive.cpp"
    "${REVCS_CORE}/ReVcsArchiveStore.cpp"
    ${SCRIPT_BINDINGS})
target_include_directories(vcspc_scm_vm PRIVATE
    include "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${REVCS_OWNED_VM}/include" "${REVCS_OWNED_VM}"
    "${REVCS_CORE}" "${REVCS_VM}/include")
target_compile_features(vcspc_scm_vm PRIVATE cxx_std_17)
target_compile_definitions(vcspc_scm_vm PRIVATE REVCS_LOCAL_VCS_VM_CORE=1)
target_link_libraries(vcspc_scm_vm PRIVATE reVcsZstd Threads::Threads)
set_target_properties(vcspc_scm_vm PROPERTIES OUTPUT_NAME "VCSPC.ScmVm" PREFIX "")
if(WIN32)
    target_sources(vcspc_scm_vm PRIVATE src/VmBridge.def)
endif()
if(MSVC)
    target_compile_options(vcspc_scm_vm PRIVATE /W4 /permissive- /EHsc)
endif()
