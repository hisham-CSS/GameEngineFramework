# Character data: schema v2 files -> memory.
#
# A SEPARATE target from Kernel on purpose, and the reason is the D2/D4 split.
# The kernel's GameState is a fixed-size POD of integers snapshotted by memcpy
# every tick. Character data is the opposite kind of thing: variable-sized,
# string-bearing, loaded once at match start and never mutated by a tick. It must
# never end up inside GameState, and keeping it in its own library makes that
# visible rather than merely intended — the kernel does not link this, and
# Kernel/CMakeLists.txt asserts it links nothing at all.
#
# The bridge between them is a small integer handle: the POD state holds a
# uint16 move index, and this library owns the table it indexes.

add_library(CseData STATIC
    src/CharacterData.cpp
    # Compiled in rather than linked. Character files are authored content and
    # docs/MAINTENANCE.md admits no exceptions: an authored path goes through
    # PathIsContained before anything opens it. The alternative to compiling this
    # one file would be linking the whole Engine, which would give character data
    # a transitive dependency on GL, Jolt and Lua — precisely what the kernel
    # spends a CMake assertion to avoid, and no more appropriate here.
    #
    # tests/CMakeLists.txt already does this with Editor/src/UndoHistory.cpp, so
    # it is the established shape rather than a new idea. PathSandbox.h includes
    # only Core.h, <filesystem> and <string>, and Core.h's ENGINE_API expands to
    # nothing when neither ENGINE_DLL_EXPORTS nor ENGINE_DLL_IMPORTS is defined —
    # which is the case here — so the symbol lands in this archive plainly.
    #
    # The tidier long-term shape is a tiny PathSandbox target both Engine and
    # this library link. That touches Engine/CMakeLists.txt and is not worth
    # doing at 2am; recorded here so the next person has the option.
    ${CMAKE_SOURCE_DIR}/Engine/src/core/PathSandbox.cpp
)

target_include_directories(CseData
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_SOURCE_DIR}/Engine/src/core   # PathSandbox.h + Core.h only
)

target_compile_features(CseData PUBLIC cxx_std_17)
set_target_properties(CseData PROPERTIES POSITION_INDEPENDENT_CODE ON)

# PRIVATE: nlohmann is an implementation detail of the loader. CharacterData.h
# includes only <cstdint>/<string>/<string_view>/<vector>, so no consumer needs
# the JSON library on its include path, and none should acquire a dependency on
# the file format just by reading a character.
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(CseData PRIVATE nlohmann_json::nlohmann_json)

# --- The same guarantee the kernel has, for the same reason -----------------
#
# CseData may grow dependencies (it already has one), but it must never acquire
# CseKernel. The dependency runs the other way: a future match-setup layer reads
# character data and fills in kernel state. If this fires, something is trying to
# put variable-sized data into the POD simulation state, which is exactly the
# mistake D4 exists to prevent.
get_target_property(_data_links CseData LINK_LIBRARIES)
if (_data_links MATCHES "CseKernel")
    message(FATAL_ERROR
        "CseData links CseKernel: ${_data_links}\n"
        "The dependency runs the other way. Character data is variable-sized and "
        "loaded once; GameState is a fixed-size POD memcpy'd every tick. See "
        "docs/ARCHITECTURE.md D2 and D4.")
endif()
unset(_data_links)
