##################################################################
#
# Bundle Protocol Library (BPLib) CMake build recipe
#
# This file allows BPLib to be configured and built via the CMake tool
# It also permits the BPLib module to be used in conjunction with CFE/CFS
#
##################################################################

cmake_minimum_required(VERSION 3.5)
project(BPLIB C)

option(BPLIB_INCLUDE_STORAGE "Whether or not to build the basic/example storage services as part of BPLib" ON)
option(BPLIB_INCLUDE_BPV6 "Whether or not to the BPv6 protocol implementation as part of BPLib" ON)
option(BPLIB_INCLUDE_BPV7 "Whether or not to the BPv7 protocol implementation as part of BPLib (EXPERIMENTAL)" OFF)
option(BPLIB_INCLUDE_POSIX "Whether or not to the POSIX operating system abstraction as part of BPLib (standalone builds only)" ON)
option(BPLIB_BUILD_TEST_TOOLS "Whether or not to build the test programs as part of BPLib (standalone builds only)" ON)

set(BPLIB_VERSION_STRING "3.0.99") # development

# source units that comprise the basic fundamentals of BPLib
# These units should always be built regardless of configuration
set(BPLIB_SRC

  # library API
  lib/bplib.c

  # other common objects
  common/crc.c
  common/rb_tree.c
  common/rh_hash.c
  common/cbuf.c
  common/lrc.c
)

# no extra link libraries at first
set(BPLIB_LINK_LIBRARIES)
set(BPLIB_PRIVATE_INCLUDE_DIRS common lib)

# Add source units that comprise the protocol implementation
# This is dependent on build configuration
if (BPLIB_INCLUDE_BPV6)

  list(APPEND BPLIB_SRC
    # Include BPv6 implementation parts
    v6/v6.c
    v6/bib.c
    v6/cteb.c
    v6/pay.c
    v6/pri.c
    v6/dacs.c
    v6/sdnv.c
  )
  list(APPEND BPLIB_PRIVATE_INCLUDE_DIRS v6)

endif()

if (BPLIB_INCLUDE_BPV7)

  # v7 implementation parts
  list(APPEND BPLIB_SRC
    v7/v7_mpool.c
    v7/v7_codec.c
  )
  list(APPEND BPLIB_PRIVATE_INCLUDE_DIRS v7)

endif()

# examples of storage implementations
# these are not strictly required to be built with bplib, as an implemenation
# may register additional or alternate implementations
if (BPLIB_INCLUDE_STORAGE)

  list(APPEND BPLIB_SRC

    store/file.c
    store/ram.c
    store/flash.c
    store/flash_sim.c

  )
  list(APPEND BPLIB_PRIVATE_INCLUDE_DIRS store)

endif()

# If building as part of CFE/CFS, then the "IS_CFS_ARCH_BUILD" should be set
# this allows simply dropping this module into a CFS project
if (IS_CFS_ARCH_BUILD)

  # Create the app module using the CFE/OSAL adapter layer
  # Note this ignores the setting of BPLIB_INCLUDE_POSIX, as OSAL provides this service
  # The CFE build system determines whether to create a shared or static object inside this routine
  add_cfe_app(bplib ${BPLIB_SRC} os/cfe.c)

else()

  # Building as a standalone library
  # This by will usually require a POSIX OS layer
  if (BPLIB_INCLUDE_POSIX)

    list(APPEND BPLIB_SRC os/posix.c)
    list(APPEND BPLIB_LINK_LIBRARIES rt pthread)

  endif()

  # Add the generic library target
  # The flavor of library (shared/static) being built here depends on the BUILD_SHARED_LIBS option
  # This directory may be built twice, setting this option differently to build both flavors
  add_library(bplib ${BPLIB_SRC})

  # compile this library as c99 (but this does not impose the same requirement on other users)
  target_compile_features(bplib PRIVATE c_std_99)

  # If using GNU GCC, then also enable full warning reporting
  target_compile_options(bplib PRIVATE $<$<C_COMPILER_ID:GNU>:-Wall -Werror -pedantic>)

  if (BPLIB_INCLUDE_BPV7)

    # Use pkg-config to locate tinycbor dependency (required only for v7)
    find_package(PkgConfig)
    pkg_search_module(TINYCBOR tinycbor)
    message(STATUS "Found tinycbor version ${TINYCBOR_VERSION}")

    list(APPEND BPLIB_LINK_LIBRARIES ${TINYCBOR_LIBRARIES})
    list(APPEND BPLIB_PRIVATE_INCLUDE_DIRS ${TINYCBOR_INCLUDE_DIRS})

  endif()



  # link with the requisite dependencies (this is mainly for POSIX, if using that adapter)
  target_link_libraries(bplib ${BPLIB_LINK_LIBRARIES})

  # Install and also export this library, so it can be found via
  # "find_package()" from some other CMake build
  install(TARGETS bplib
    EXPORT bplib-export
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(EXPORT bplib-export
    FILE BPLibTargets.cmake
    DESTINATION lib/cmake/bplib
  )
  install(DIRECTORY inc/
    DESTINATION include/bplib
  )

  # Set API/ABI version info if building a shared library
  if(BUILD_SHARED_LIBS)

    set_target_properties(bplib PROPERTIES
      SOVERSION "${BPLIB_VERSION_STRING}"
    )

    # If using GNU, then also add ABI version to final link
    if ("${CMAKE_C_COMPILER_ID}" STREQUAL "GNU")
      set_target_properties(bplib PROPERTIES
        LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libabi.version"
      )
    endif()

  endif()

  # Build the test tools, if directed
  if (BPLIB_BUILD_TEST_TOOLS)
    add_subdirectory(app)
  endif()

endif()

# Internal/private header files exist within the implementation directories
target_include_directories(bplib PRIVATE ${BPLIB_PRIVATE_INCLUDE_DIRS})

# The API to this library (which may be invoked/referenced from other apps)
# is stored in fsw/public_inc.  Using "target_include_directories" is the
# preferred method of indicating this (vs. directory-scope "include_directories").
target_include_directories(bplib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
  $<INSTALL_INTERFACE:include/bplib>
)
