# mod-mythic-plus configure-time hooks (included by modules/CMakeLists.txt, OPTIONAL).
set(_mp_dir "${CMAKE_CURRENT_LIST_DIR}")

# Linkage of this module (static, dynamic or disabled), resolved by modules/CMakeLists.txt before this include.
ModuleNameToVariable("mod-mythic-plus" _mp_link_var)
set(_mp_link "${${_mp_link_var}}")

# --- Bot provider switch (spec §4): ON compiles the bot seam with no providers.
# The definition attaches to the static `modules` target only. With MODULES=dynamic (or this module set to
# dynamic) the module is its own shared library and the switch has no effect.
option(MP_NO_BOT_PROVIDERS "mod-mythic-plus: compile the bot seam with no providers" OFF)
if(MP_NO_BOT_PROVIDERS)
  target_compile_definitions(modules PRIVATE MP_NO_BOT_PROVIDERS)
endif()
if(NOT _mp_link STREQUAL "static")
  message(STATUS "mod-mythic-plus: linkage is '${_mp_link}', not static; MP_NO_BOT_PROVIDERS (attached to the "
    "static modules target) is not applied")
endif()

# --- Config sync (spec §3.4): keys AND default values in MpConfig.cpp must equal conf.dist.
set(_mp_cfg_src "${_mp_dir}/src/Core/MpConfig.cpp")
set(_mp_conf "${_mp_dir}/conf/mod-mythic-plus.conf.dist")
function(_mp_canon in out)
  string(STRIP "${in}" v)
  string(REGEX REPLACE "f$" "" v "${v}")
  if(v STREQUAL "true")
    set(v 1)
  elseif(v STREQUAL "false")
    set(v 0)
  endif()
  if(v MATCHES "\\.")
    string(REGEX REPLACE "0+$" "" v "${v}")
    string(REGEX REPLACE "\\.$" "" v "${v}")
  endif()
  set(${out} "${v}" PARENT_SCOPE)
endfunction()

if(EXISTS "${_mp_cfg_src}")
  set(_mp_drift "")
  file(STRINGS "${_mp_cfg_src}" _lines REGEX "\"MythicPlus\\.[A-Za-z0-9_.]+\", *[-0-9.a-z]+")
  set(_code_keys "")
  foreach(_l IN LISTS _lines)
    string(REGEX MATCH "\"(MythicPlus\\.[A-Za-z0-9_.]+)\", *([-0-9.a-z]+)" _m "${_l}")
    set(_k "${CMAKE_MATCH_1}")
    _mp_canon("${CMAKE_MATCH_2}" _v)
    list(APPEND _code_keys "${_k}")
    set(_code_${_k} "${_v}")
  endforeach()
  file(STRINGS "${_mp_conf}" _clines REGEX "^[ \t]*MythicPlus\\.[A-Za-z0-9_.]+[ \t]*=")
  set(_conf_keys "")
  foreach(_l IN LISTS _clines)
    string(REGEX MATCH "(MythicPlus\\.[A-Za-z0-9_.]+)[ \t]*=[ \t]*([^ \t#]*)" _m "${_l}")
    set(_k "${CMAKE_MATCH_1}")
    _mp_canon("${CMAKE_MATCH_2}" _v)
    list(APPEND _conf_keys "${_k}")
    set(_conf_${_k} "${_v}")
  endforeach()
  foreach(_k IN LISTS _code_keys)
    if(NOT DEFINED _conf_${_k})
      list(APPEND _mp_drift "missing in conf.dist: ${_k}")
    elseif(NOT "${_code_${_k}}" STREQUAL "${_conf_${_k}}")
      list(APPEND _mp_drift "${_k}: code=${_code_${_k}} conf=${_conf_${_k}}")
    endif()
  endforeach()
  foreach(_k IN LISTS _conf_keys)
    if(NOT DEFINED _code_${_k})
      list(APPEND _mp_drift "never read: ${_k}")
    endif()
  endforeach()
  if(_mp_drift)
    string(REPLACE ";" "\n  " _mp_drift_txt "${_mp_drift}")
    message(WARNING "mod-mythic-plus config drift:\n  ${_mp_drift_txt}")
  else()
    list(LENGTH _code_keys _n)
    message(STATUS "mod-mythic-plus: ${_n} config keys and defaults in sync with conf.dist")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mp_cfg_src}" "${_mp_conf}")
endif()

# --- Every hook must override a real ScriptMgr hook (spec §3.3d). Clang flags: the docker build uses clang.
# Two upstream headers declare overrides without `override` (G3D/MemoryManager.h, Roll in Group.h); they are
# treated as system headers for module TUs so -Wsuggest-override only judges module code.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # These options are set on this module's .cpp files only: the --system-header-prefix entries (which match any
  # include spelled "G3D/..." or "Group.h...") never apply to core or other modules' translation units.
  # They are source properties of the modules/ directory (TARGET_DIRECTORY modules), not of the `modules` target.
  # A dynamic module's shared library is created in that same directory, so they should apply there too, but only
  # the static build (the docker default) is verified.
  set(_mp_sys_headers "--system-header-prefix=G3D/;--system-header-prefix=Group.h")
  file(GLOB_RECURSE _mp_tus "${_mp_dir}/src/*.cpp")
  set_source_files_properties(${_mp_tus} TARGET_DIRECTORY modules PROPERTIES
    COMPILE_OPTIONS "-Werror=inconsistent-missing-override;-Werror=suggest-override;${_mp_sys_headers}")
else()
  message(STATUS "mod-mythic-plus: override checks (-Werror=suggest-override) are Clang-only; "
    "skipped for ${CMAKE_CXX_COMPILER_ID}")
endif()
