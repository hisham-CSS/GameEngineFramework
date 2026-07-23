# AssetCooker: headless asset-work process (P4-3 phase 4). Console
# subsystem, links Engine but never initializes GL — the out-of-process
# seam for batch asset work (validation now; BC7 texture cooking with
# P4-2; packing later). Spawned by the editor as a child process.
add_executable(AssetCooker src/CookerMain.cpp)
target_link_libraries(AssetCooker PRIVATE Engine)
target_compile_definitions(AssetCooker PRIVATE GLFW_INCLUDE_NONE)

# App icon (see resources/app.rc; regenerate via scripts/make_icon.py)
if(WIN32)
    target_sources(AssetCooker PRIVATE ${CMAKE_SOURCE_DIR}/resources/app.rc)
endif()

# Same output dir as the Editor/Player: shares Engine.dll + Exported/,
# and the editor finds it next to its own exe.
set_target_properties(AssetCooker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/build/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/build/bin/Release
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/build/bin/RelWithDebInfo
)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(AssetCooker PRIVATE -g)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(AssetCooker PRIVATE /Zi)
endif()

add_dependencies(AssetCooker runtime_assets)
