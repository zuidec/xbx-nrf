# Build ID: `git describe` over one component's tags only (<prefix>-v*), so
# another component's or the hardware's tag doesn't show up in it (Zephyr's
# APP_BUILD_VERSION takes the nearest tag of any kind). Generates
# xbx_build_id.h with XBX_BUILD_ID on every build (a new tag doesn't touch
# the git index); rewritten only when it changes, so nothing recompiles.
#
# In an app's CMakeLists, after project():
#   include(../common/build_id.cmake)
#   xbx_build_id(controller)
# Run by the build itself with -P (script mode) to write the header.

if(CMAKE_SCRIPT_MODE_FILE)
  execute_process(
    COMMAND git describe --match "${PREFIX}-v*" --abbrev=12 --always
    WORKING_DIRECTORY ${DIR}
    OUTPUT_VARIABLE id
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE err)
  if(err OR NOT id)
    set(id "unknown")
  endif()
  set(content "#define XBX_BUILD_ID \"${id}\"\n")
  if(EXISTS ${OUT})
    file(READ ${OUT} old)
  endif()
  if(NOT old STREQUAL content)
    file(WRITE ${OUT} "${content}")
  endif()
  return()
endif()

function(xbx_build_id prefix)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/xbx_build_id/xbx_build_id.h)
  add_custom_target(xbx_build_id
    COMMAND ${CMAKE_COMMAND} -DPREFIX=${prefix} -DOUT=${out}
      -DDIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_FUNCTION_LIST_FILE}
    BYPRODUCTS ${out}
    VERBATIM)
  add_dependencies(app xbx_build_id)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/xbx_build_id)
endfunction()
