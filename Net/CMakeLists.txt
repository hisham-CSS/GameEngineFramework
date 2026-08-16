# The rollback session seam.
#
# Deliberately a separate target from both Kernel and Engine, and its GekkoNet
# dependency is PRIVATE. That combination is the seam:
#
#   * PRIVATE means gekkonet.h reaches exactly one translation unit
#     (GekkoSession.cpp) and nothing that links CseNet can see it, so no other
#     file in the project can accidentally grow a dependency on the concrete
#     implementation. Replacing GekkoNet becomes "rewrite one .cpp".
#
#   * Separate from the kernel because the kernel links NOTHING (see
#     Games/UntitledFighter/Kernel/CMakeLists.txt and its configure-time
#     assertion). The session is not part of the
#     authoritative simulation; it TELLS the simulation when to run. Putting it in
#     the kernel would break the guarantee that makes the kernel portable.
#
#   * ...and it stays at the top level rather than moving under Games/ with the
#     fighting game that motivated it, because ISession names no fighter. Any
#     deterministic game wants rollback. See the header of
#     Games/UntitledFighter/CMakeLists.txt for that test.
#
#   * Separate from Engine because the session has nothing to do with rendering
#     and the shipped dedicated/headless case should be able to link it alone.
#
# Note CseNet does not link CseKernel either, and that is not an oversight:
# ISession.h moves bytes and a length, never a GameState (ADR-002 CHOICE A). The
# HOST owns both and wires them together. If CseNet ever needs to link CseKernel,
# something has leaked.

add_library(CseNet STATIC
    src/GekkoSession.cpp
)

target_include_directories(CseNet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(CseNet PUBLIC cxx_std_17)
set_target_properties(CseNet PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(CseNet PRIVATE GekkoNet)

# --- The seam, enforced -----------------------------------------------------
#
# Same idea as Kernel's link assertion, aimed at a different failure. GekkoNet
# must never become a PUBLIC or INTERFACE dependency: the moment it does,
# gekkonet.h lands on the include path of everything downstream, someone reaches
# for a GekkoNet type directly, and the "replace one .cpp" property is gone
# quietly.
#
# The check is fiddlier than it looks, and the first version of it was WRONG in
# the noisy direction — it fired on a correctly PRIVATE dependency. For a STATIC
# library, CMake puts PRIVATE link libraries into INTERFACE_LINK_LIBRARIES
# wrapped as $<LINK_ONLY:GekkoNet>: a static archive does not contain its
# dependencies, so consumers must still LINK them at the final link, even though
# nothing about the interface (includes, defines) propagates. So
# `$<LINK_ONLY:X>` is exactly what correct looks like, and a bare `X` is the
# violation. Filter the wrapped ones out and fail on whatever is left.
get_target_property(_net_iface CseNet INTERFACE_LINK_LIBRARIES)
if (_net_iface)
    set(_leaked "")
    foreach (_dep IN LISTS _net_iface)
        if (NOT _dep MATCHES "^\\$<LINK_ONLY:")
            list(APPEND _leaked "${_dep}")
        endif()
    endforeach()
    if (_leaked)
        message(FATAL_ERROR
            "CseNet propagates a link dependency to its consumers: ${_leaked}\n"
            "GekkoNet must stay PRIVATE so gekkonet.h reaches only "
            "GekkoSession.cpp. See ADR-002 CHOICE A on keeping the session layer "
            "replaceable, and ADR-003 on why the seam is event-pump shaped.")
    endif()
    unset(_leaked)
endif()
unset(_net_iface)

# The property that actually matters to a reader of ISession.h: GekkoNet's
# headers must not be reachable from anything that links CseNet. Asserted
# directly rather than inferred from the link type, because this is the thing we
# care about and the link type is only a proxy for it.
get_target_property(_net_incs CseNet INTERFACE_INCLUDE_DIRECTORIES)
foreach (_inc IN LISTS _net_incs)
    if (_inc MATCHES "GekkoNet")
        message(FATAL_ERROR
            "CseNet exports a GekkoNet include directory: ${_inc}\n"
            "Consumers would be able to #include <gekkonet.h>, which defeats the "
            "seam. Keep GekkoNet's includes PRIVATE.")
    endif()
endforeach()
unset(_net_incs)
