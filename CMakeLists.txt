cmake_minimum_required(VERSION 3.14)
project(ScGrabber C)

option(USE_OPTIONAL "Enable the _Optional qualifier" OFF)
option(USE_FORTIFY "Enable Fortify memory-allocation checking" OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(CB
  GIT_REPOSITORY https://github.com/chrisbazley/CBLibrary.git
  GIT_TAG main
)
FetchContent_Declare(CBOS
  GIT_REPOSITORY https://github.com/chrisbazley/CBOSLib.git
  GIT_TAG main
)
FetchContent_Declare(CBDebug
  GIT_REPOSITORY https://github.com/chrisbazley/CBDebugLib.git
  GIT_TAG main
)
FetchContent_Declare(OptionalAcornC
  GIT_REPOSITORY https://github.com/chrisbazley/OptionalAcornC.git
  GIT_TAG main
)
FetchContent_MakeAvailable(CB CBOS CBDebug OptionalAcornC)

if(USE_OPTIONAL)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR
      "USE_OPTIONAL requires Christopher Bazley's Clang fork")
  endif()
  add_compile_definitions(USE_OPTIONAL)

  if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")
    set(CMAKE_XCODE_ATTRIBUTE_CLANG_ANALYZER_OTHER_FLAGS
      "-analyzer-checker optionality,core -analyzer-max-loop 32")
  endif()
endif()

if(USE_FORTIFY)
  FetchContent_Declare(Fortify
    GIT_REPOSITORY https://github.com/chrisbazley/Fortify.git
    GIT_TAG main
  )
  FetchContent_MakeAvailable(Fortify)
  link_libraries(Fortify)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")

if(MSVC)
  add_compile_options(/W4 /wd4200 /wd4996 /wd4130)
else()
  add_compile_options(-Wall -Wextra -Wsign-compare
    -Wno-unused-parameter -pedantic)
endif()

if(ENABLE_CLANG_TIDY)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    message(FATAL_ERROR "ENABLE_CLANG_TIDY requires Clang")
  endif()
  find_program(CLANG_TIDY_EXE NAMES clang-tidy
    HINTS "/opt/homebrew/opt/llvm/bin")
  if(NOT CLANG_TIDY_EXE)
    message(FATAL_ERROR "ENABLE_CLANG_TIDY is ON, but clang-tidy was not found")
  endif()
  set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_EXE}"
    "--checks=-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.*"
    "--warnings-as-errors=clang-analyzer-optionality.*")
endif()

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

set(FRONTEND_SOURCES
  SGFrontEnd.c MakeConfig.c FEutils.c KeyNames.c ConfigFile.c SetupDbox.c
)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  add_executable(ScGrabberApp ${FRONTEND_SOURCES})
else()
  # Toolbox applications can only be linked and run on RISC OS.
  add_library(ScGrabberApp OBJECT ${FRONTEND_SOURCES})
endif()

target_link_libraries(ScGrabberApp PRIVATE CB CBOS CBDebug CBUtil OptionalAcornC)
target_compile_definitions(ScGrabberApp PRIVATE
  $<$<CONFIG:Debug>:DEBUG_OUTPUT>
)

# The module needs CMHG and assembler inputs for its native image. Compile
# its C source on every platform, including cross-platform syntax checks.
add_library(ScGrabberModule OBJECT ScrGrabber.c Errors.c ScrGrabberHdr.h)
target_link_libraries(ScGrabberModule PRIVATE CB CBDebug CBUtil OptionalAcornC)
target_compile_definitions(ScGrabberModule PRIVATE
  $<$<CONFIG:Debug>:DEBUG_OUTPUT>
)
