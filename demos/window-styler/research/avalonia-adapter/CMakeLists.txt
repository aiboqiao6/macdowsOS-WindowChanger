cmake_minimum_required(VERSION 3.22)
project(AvaloniaOriginalTree LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
set(revision "${CMAKE_SIZEOF_VOID_P}")
foreach(file native.cpp backend.cs bridge.cs Backend.csproj Bridge.csproj CMakeLists.txt ../../protocol.h ../../adapter-api.h)
    file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/${file}" part)
    string(APPEND revision "${part}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
endforeach()
string(SHA256 revision "${revision}")
string(SUBSTRING "${revision}" 0 12 revision)
set(bridge "MacdowsAvaloniaBridge-${revision}")
set(backend "MacdowsAvaloniaTree-${revision}")
set(managed "${CMAKE_BINARY_DIR}/managed/${revision}")
add_custom_command(OUTPUT "${managed}/${bridge}.dll" "${managed}/${backend}.dll"
    COMMAND ${DOTNET_EXECUTABLE} build "${CMAKE_CURRENT_SOURCE_DIR}/Bridge.csproj" -c Release -o "${managed}"
        "-p:AssemblyName=${bridge}" "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/obj/bridge/"
    COMMAND ${DOTNET_EXECUTABLE} build "${CMAKE_CURRENT_SOURCE_DIR}/Backend.csproj" -c Release -o "${managed}"
        "-p:AssemblyName=${backend}" "-p:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/obj/backend/"
    DEPENDS bridge.cs backend.cs Bridge.csproj Backend.csproj VERBATIM)
add_custom_target(AvaloniaManaged DEPENDS "${managed}/${bridge}.dll" "${managed}/${backend}.dll")
add_library(MacdowsAvaloniaInline SHARED native.cpp)
target_include_directories(MacdowsAvaloniaInline PRIVATE ../..)
target_compile_definitions(MacdowsAvaloniaInline PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    STYLER_AVALONIA_BRIDGE=L"${bridge}" STYLER_AVALONIA_TREE=L"${backend}.dll")
target_link_libraries(MacdowsAvaloniaInline PRIVATE comctl32)
set_target_properties(MacdowsAvaloniaInline PROPERTIES
    OUTPUT_NAME "MacdowsAvaloniaInline-${revision}-$<LOWER_CASE:$<CONFIG>>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
add_dependencies(MacdowsAvaloniaInline AvaloniaManaged)
add_custom_command(TARGET MacdowsAvaloniaInline POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${managed}/${bridge}.dll" "${managed}/${backend}.dll"
        "${managed}/${bridge}.runtimeconfig.json" "$<TARGET_FILE_DIR:MacdowsAvaloniaInline>" VERBATIM)
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(avalonia_bridge_name "${bridge}" PARENT_SCOPE)
    set(avalonia_tree_name "${backend}" PARENT_SCOPE)
endif()
