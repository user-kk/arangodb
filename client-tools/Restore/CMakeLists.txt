add_library(arango_restore
  RestoreFeature.cpp
)
target_include_directories(arango_restore PUBLIC ${PROJECT_SOURCE_DIR}/client-tools)
if (USE_ENTERPRISE)
  target_include_directories(arango_restore PUBLIC "${PROJECT_SOURCE_DIR}/${ENTERPRISE_INCLUDE_DIR}")
endif()
target_link_libraries(arango_restore arango_shell)

add_executable(${BIN_ARANGORESTORE}
  ${ProductVersionFiles_arangorestore}
  arangorestore.cpp
)

target_link_libraries(${BIN_ARANGORESTORE}
  arango_restore
  arango
  ${SYSTEM_LIBRARIES}
  boost_system
  boost_boost
  arango_shell
  clienttools_utils
)

install(
  TARGETS ${BIN_ARANGORESTORE}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install_config(${BIN_ARANGORESTORE})

add_dependencies(${BIN_ARANGORESTORE} zlibstatic)

if (NOT USE_PRECOMPILED_V8)
  add_dependencies(${BIN_ARANGORESTORE} v8_build) # v8_build includes ICU build
endif ()

if (USE_JEMALLOC)
  add_dependencies(${BIN_ARANGORESTORE} jemalloc_build)
endif ()

if (UNIX) 
  add_dependencies(${BIN_ARANGORESTORE} man)
endif ()
