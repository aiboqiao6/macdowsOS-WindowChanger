# The same exact ABI translation and pre-call verifier are used by production.
# This registry and these explicit-target hosts remain isolated research tools.
include("${root}/office-21048-profile.cmake")
office_21048_sources(office_21048_dir "${root}" "${research_hook_name}")
get_filename_component(office_21048_digest "${office_21048_dir}" NAME)
add_library(Office21048LeaseProbe SHARED "${office_21048_dir}/office.cpp")
target_compile_definitions(Office21048LeaseProbe PRIVATE OFFICE_LEASE_EXPERIMENT)
target_link_libraries(Office21048LeaseProbe PRIVATE gdi32 dwmapi comctl32)
set_target_properties(Office21048LeaseProbe PROPERTIES
    OUTPUT_NAME "Office21048LeaseProbe-${office_21048_digest}-$<LOWER_CASE:$<CONFIG>>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/office-21048-research")
add_executable(Office21048BackgroundHost "${CMAKE_CURRENT_LIST_DIR}/office-background-host.cpp")
target_link_libraries(Office21048BackgroundHost PRIVATE styler_session)
add_dependencies(Office21048BackgroundHost MacdowsStyleHook Office21048LeaseProbe)
set_target_properties(Office21048BackgroundHost PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/office-21048-research")
add_custom_command(TARGET Office21048BackgroundHost POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:MacdowsStyleHook> $<TARGET_FILE_DIR:Office21048BackgroundHost>)
# Reuse the complete original-route/capture-cancellation/owner-loss verifier.
# Its crash child resolves OfficeLeaseHost.exe in the same isolated directory.
add_executable(Office21048ForegroundHost "${CMAKE_CURRENT_LIST_DIR}/office-lease-host.cpp")
target_link_libraries(Office21048ForegroundHost PRIVATE styler_session)
add_dependencies(Office21048ForegroundHost MacdowsStyleHook Office21048LeaseProbe)
set_target_properties(Office21048ForegroundHost PROPERTIES OUTPUT_NAME OfficeLeaseHost
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/office-21048-research")
add_custom_command(TARGET Office21048ForegroundHost POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:MacdowsStyleHook> $<TARGET_FILE_DIR:Office21048ForegroundHost>)
file(GENERATE OUTPUT "$<TARGET_FILE_DIR:Office21048BackgroundHost>/adapters.ini" CONTENT
    "[Adapters]\nIds=272\n[Adapter272]\nName=Office 16.0.14334.21048 isolated research\nChildClass=NetUIHWND\nRequiredModules=mso40uiwin32client.dll\nFile=$<TARGET_FILE_NAME:Office21048LeaseProbe>\n")
