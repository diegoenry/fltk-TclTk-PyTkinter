cmake_minimum_required(VERSION 3.15)
project(fltk_console LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Homebrew prefixes (macOS arm64) ──────────────────────────────
set(HOMEBREW /opt/homebrew/opt)

# ── FLTK (always use fltk-config for reliable macOS framework linkage) ──
execute_process(
    COMMAND ${HOMEBREW}/fltk/bin/fltk-config --cxxflags
    OUTPUT_VARIABLE FLTK_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND ${HOMEBREW}/fltk/bin/fltk-config --ldflags
    OUTPUT_VARIABLE FLTK_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)

separate_arguments(FLTK_CXX_FLAGS)
set(FLTK_INCLUDE_DIR ${HOMEBREW}/fltk/include)

# ── Tcl ──────────────────────────────────────────────────────────
set(TCL_PREFIX ${HOMEBREW}/tcl-tk)
set(TCL_INCLUDE_DIR ${TCL_PREFIX}/include/tcl-tk)
find_library(TCL_LIBRARY NAMES tcl9.0 tcl PATHS ${TCL_PREFIX}/lib NO_DEFAULT_PATH)

# ── Python ───────────────────────────────────────────────────────
find_package(Python3 COMPONENTS Development.Embed QUIET)
if(NOT Python3_FOUND)
    # Manual fallback
    execute_process(
        COMMAND python3 -c "import sysconfig; print(sysconfig.get_config_var('INCLUDEPY'))"
        OUTPUT_VARIABLE Python3_INCLUDE_DIRS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND python3 -c "import sysconfig; print(sysconfig.get_config_var('LIBDIR'))"
        OUTPUT_VARIABLE _pylib OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(
        COMMAND python3 -c "import sysconfig; v=sysconfig.get_config_var; print(f'{v(\"LIBDIR\")}/{v(\"LDLIBRARY\")}')"
        OUTPUT_VARIABLE Python3_LIBRARIES OUTPUT_STRIP_TRAILING_WHITESPACE)
    # On macOS with framework builds, we need the framework path
    execute_process(
        COMMAND python3 -c "import sysconfig; print(sysconfig.get_config_var('PYTHONFRAMEWORKPREFIX') or '')"
        OUTPUT_VARIABLE _pyframework OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# ── Executable ───────────────────────────────────────────────────
add_executable(fltk_console
    src/main.cpp
    src/console_window.cpp
    src/tcl_console.cpp
    src/python_console.cpp
    src/graph_params.cpp
    src/graph_window.cpp
    src/plugin_process.cpp
)

# ── Include dirs ─────────────────────────────────────────────────
target_include_directories(fltk_console PRIVATE
    ${FLTK_INCLUDE_DIR}
    ${TCL_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
)

# ── Compile flags ────────────────────────────────────────────────
if(DEFINED FLTK_CXX_FLAGS)
    target_compile_options(fltk_console PRIVATE ${FLTK_CXX_FLAGS})
endif()

# ── Link ─────────────────────────────────────────────────────────
# FLTK: pass raw ldflags via LINK_FLAGS to preserve "-framework X" pairs
set_target_properties(fltk_console PROPERTIES LINK_FLAGS "${FLTK_LD_FLAGS}")

target_link_libraries(fltk_console PRIVATE ${TCL_LIBRARY})

if(Python3_FOUND)
    target_link_libraries(fltk_console PRIVATE Python3::Python)
else()
    target_link_libraries(fltk_console PRIVATE ${Python3_LIBRARIES})
    if(_pyframework)
        target_link_options(fltk_console PRIVATE "-F${_pyframework}")
    endif()
endif()

# ── Tests ────────────────────────────────────────────────────────
enable_testing()

add_executable(test_interpreters
    tests/test_interpreters.cpp
    src/graph_params.cpp
)

target_include_directories(test_interpreters PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${TCL_INCLUDE_DIR}
    ${Python3_INCLUDE_DIRS}
)

target_link_libraries(test_interpreters PRIVATE ${TCL_LIBRARY})
if(Python3_FOUND)
    target_link_libraries(test_interpreters PRIVATE Python3::Python)
else()
    target_link_libraries(test_interpreters PRIVATE ${Python3_LIBRARIES})
    if(_pyframework)
        target_link_options(test_interpreters PRIVATE "-F${_pyframework}")
    endif()
endif()

add_test(NAME interpreters COMMAND test_interpreters)

# ── Status ───────────────────────────────────────────────────────
message(STATUS "FLTK include: ${FLTK_INCLUDE_DIR}")
message(STATUS "TCL library:  ${TCL_LIBRARY}")
message(STATUS "Python include: ${Python3_INCLUDE_DIRS}")
message(STATUS "Python library: ${Python3_LIBRARIES}")
