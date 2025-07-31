# Helper function to get name of target property into variable TYPE
function(getTarget target type)
  get_target_property(IMPORTED_TARGET ${target} IMPORTED)
  if(IMPORTED_TARGET)
    set(${type}
        INTERFACE
        PARENT_SCOPE)
  else()
    set(${type}
        PRIVATE
        PARENT_SCOPE)
  endif()
endfunction()

# Set compile options for C language
function(addCFlag)
  if(ARGC EQUAL 1)
    add_compile_options($<$<COMPILE_LANGUAGE:C>:${ARGV0}>)
  elseif(ARGC EQUAL 2)
    gettarget(${ARGV1} TYPE)
    target_compile_options(${ARGV1} ${TYPE} $<$<COMPILE_LANGUAGE:C>:${ARGV0}>)
  endif()
endfunction()

# Set compile options for C++ language
function(addCXXFlag)
  if(ARGC EQUAL 1)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${ARGV0}>)
  elseif(ARGC EQUAL 2)
    gettarget(${ARGV1} TYPE)
    target_compile_options(${ARGV1} ${TYPE} $<$<COMPILE_LANGUAGE:CXX>:${ARGV0}>)
  endif()
endfunction()

# Add link flag to all targets or just specified target
function(addLinkerFlag)
  if(ARGC EQUAL 1)
    add_link_options(${ARGV0})
  elseif(ARGC EQUAL 2)
    gettarget(${ARGV1} TYPE)
    target_link_options(${ARGV1} ${TYPE} ${ARGV0})
  endif()
endfunction()

# Helper to set both C and C++ flags
function(addCCXXFlag)
  addcflag(${ARGV0} ${ARGV1})
  addcxxflag(${ARGV0} ${ARGV1})
endfunction()

# Helper to set flag for both C, C++ and ObjC
function(addCommonFlag)
  addcflag(${ARGV0} ${ARGV1})
  addcxxflag(${ARGV0} ${ARGV1})
endfunction()

# Helper
macro(CONFIGURE_CMAKE)
  message(STATUS "Configuring BB Floppy v${BB_FLOPPY_VERSION}")

  set(CMAKE_POSITION_INDEPENDENT_CODE
      ON
      CACHE BOOL "Enable position independent code for all targets" FORCE)

  # Configure use of recommended build tools
  message(STATUS "Configuring CMake to use recommended build tools...")
endmacro()

# Helper to set basic config
function(configureProject)
  # Enable C and C++ languages
  enable_language(C CXX)
  set(IMHEX_MAIN_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}"
      PARENT_SCOPE)
endfunction()

# Helper to set debug build
macro(SET_DEFAULT_BUILD_TYPE_IF_UNSET)
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE
        "RelWithDebInfo"
        CACHE STRING "Using RelWithDebInfo build type as it was left unset"
              FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug"
                                                 "RelWithDebInfo")
  endif()
endmacro()

# Helper to set version
function(loadVersion version plain_version)
  set(version_file "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${version_file})
  file(READ "${version_file}" read_version)
  string(STRIP ${read_version} read_version)
  string(REPLACE ".WIP" "" read_version_plain ${read_version})
  set(${version}
      ${read_version}
      PARENT_SCOPE)
  set(${plain_version}
      ${read_version_plain}
      PARENT_SCOPE)
endfunction()

# Helper to set variable in PARENT_SCOPE if applicable
macro(SET_VARIABLE_IN_PARENT variable value)
  get_directory_property(hasParent PARENT_DIRECTORY)

  if(hasParent)
    set(${variable}
        "${value}"
        PARENT_SCOPE)
  else()
    set(${variable} "${value}")
  endif()
endmacro()

# Helper to set all default compiler flags
macro(SETUP_COMPILER_FLAGS target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    addcommonflag("-Wall" ${target})
    addcommonflag("-Wextra" ${target})
    addcommonflag("-Wpedantic" ${target})

    # Define strict compilation flags
    if(IMHEX_STRICT_WARNINGS)
      addcommonflag("-Werror" ${target})
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
      addcommonflag("-rdynamic" ${target})
    endif()

    addcxxflag("-fexceptions" ${target})
    addcxxflag("-frtti" ${target})
    addcommonflag("-fno-omit-frame-pointer" ${target})

    # Disable some warnings
    addccxxflag("-Wno-array-bounds" ${target})
    addccxxflag("-Wno-deprecated-declarations" ${target})
    addccxxflag("-Wno-unknown-pragmas" ${target})
    addcxxflag("-Wno-include-angled-in-module-purview" ${target})
  endif()

  # Disable some warnings for gcc
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    addccxxflag("-Wno-restrict" ${target})
    addccxxflag("-Wno-stringop-overread" ${target})
    addccxxflag("-Wno-stringop-overflow" ${target})
    addccxxflag("-Wno-dangling-reference" ${target})
  endif()

  # Only generate minimal debug information for stacktraces in RelWithDebInfo
  # builds
  if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      addccxxflag("-g1" ${target})
    endif()

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Add flags for debug info in inline functions
      addccxxflag("-gstatement-frontiers" ${target})
      addccxxflag("-ginline-points" ${target})
    endif()
  endif()
endmacro()
