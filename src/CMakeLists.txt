set(TARGET QuarryAccess)

file(GLOB_RECURSE QA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")

add_library(${TARGET} SHARED ${QA_SOURCES})

target_include_directories(${TARGET}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_SOURCE_DIR}/third_party/tolk")

target_link_libraries(${TARGET} PUBLIC UE4SS PRIVATE winmm ole32)

target_compile_definitions(${TARGET} PRIVATE
    QA_VERSION_STRING="${QA_VERSION}"
    RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
    RC_UNREAL_DISABLE_CLASS_DEPRECATION_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE)

set_target_properties(${TARGET} PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    OUTPUT_NAME "main")

if(MSVC)
    target_compile_options(${TARGET} PRIVATE /W4 /utf-8 /permissive-)
    # A linker map next to main.dll lets crash offsets from minidumps be resolved to functions.
    target_link_options(${TARGET} PRIVATE /MAP)
endif()

if(QA_GAME_DIR)
    set(QA_MOD_DLL_DIR "${QA_GAME_DIR}/SMG026/Binaries/Win64/Mods/QuarryAccess/dlls")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${QA_MOD_DLL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${QA_MOD_DLL_DIR}/main.dll"
        COMMENT "Copying main.dll to ${QA_MOD_DLL_DIR}")
endif()
