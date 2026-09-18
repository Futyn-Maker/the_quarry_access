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
    # main.dll names its symbol file without the folder it was built in.
    target_link_options(${TARGET} PRIVATE /MAP /PDBALTPATH:%_PDB%)
endif()
