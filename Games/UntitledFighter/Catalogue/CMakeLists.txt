# The showcase catalogue cooker (ROADMAP M1.6, ADR-011 section 4): a console
# tool that turns every catalogue entry into artifacts -- both verdicts, a
# ReplayVerifier-verified .csrp, a graph.dot, one summary file.
#
# TITLE-OWNED ON PURPOSE, and the name collision is resolved here in writing:
# ROADMAP and the ADRs call this step "the cooker", but it may not live in
# Cooker/ -- CLAUDE.md's never-list forbids AssetCooker depending on a title,
# and the root CMakeLists enforces that at configure time with a FATAL_ERROR.
# The catalogue is title content cooked by title code, so the tool lives with
# the title and links the title's own libraries, which is the direction the
# boundary allows.
add_executable(UntitledFighterCatalogue src/CatalogueMain.cpp)
target_link_libraries(UntitledFighterCatalogue PRIVATE CseGame CseData CseKernel)

# Same output dir as the other executables: shares Exported/ so a bare
# `UntitledFighterCatalogue Exported/Characters out` works from bin.
set_target_properties(UntitledFighterCatalogue PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
)
