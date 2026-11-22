# Deliberately isolated from original-framework-adapters.cmake and production
# registries. Reuse the guarded layout/rollback implementation; change only the
# exact module triplet and independently resolved function RVAs in generated
# research sources. Unknown identities/instructions still fail before any call.
set(office_21048_files office.cpp office-layout.h office-lease.h office-edit-title.h)
set(office_21048_digest "${research_hook_name}")
foreach(name IN LISTS office_21048_files ITEMS office-21048-verify.h office-background-host.cpp office-21048-research.cmake)
    set(source "${CMAKE_CURRENT_LIST_DIR}/${name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(SHA256 "${source}" part)
    string(APPEND office_21048_digest "${part}")
endforeach()
string(SHA256 office_21048_digest "${office_21048_digest}")
string(SUBSTRING "${office_21048_digest}" 0 12 office_21048_digest)
set(office_21048_dir "${CMAKE_BINARY_DIR}/office-21048-source/${office_21048_digest}")
set(office_21048_rvas
    0x49c30:0x49ee0 0x484c0:0x48770 0x285fa0:0x289330 0x4ab00:0x4adb0
    0x135480:0x135690 0x9cba0:0x9cdb0 0xb82a0:0xb84b0 0xe4580:0xe4790
    0x100420:0x100630 0x170340:0x170500 0x1c1180:0x1c1390 0x72808:0x72a18)
foreach(name IN LISTS office_21048_files)
    file(READ "${CMAKE_CURRENT_LIST_DIR}/${name}" source)
    foreach(mapping IN LISTS office_21048_rvas)
        string(REPLACE ":" ";" pair "${mapping}")
        list(GET pair 0 before)
        list(GET pair 1 after)
        string(REGEX REPLACE "${before}([^0-9a-fA-F]|$)" "${after}\\1" source "${source}")
    endforeach()
    if(name STREQUAL "office.cpp")
        string(REPLACE
            "0x2d264595, 0x7ea8, 0x4318, {0x8a, 0xa1, 0x89, 0x76, 0x91, 0xf9, 0xd8, 0xee}"
            "0x3e4b8c5a, 0xb16a, 0x4de5, {0xb0, 0x63, 0x33, 0x8e, 0xdf, 0x2b, 0xb5, 0x46}" source "${source}")
        string(REPLACE
            "0x7da1c564, 0x0bee, 0x44f1, {0x83, 0x3b, 0x50, 0x36, 0x83, 0x82, 0x11, 0xd4}"
            "0x587d5bed, 0x5243, 0x4e42, {0x9e, 0x0e, 0x87, 0xc8, 0x7c, 0x6a, 0x4e, 0x20}" source "${source}")
        string(REPLACE
            "0x609d7177, 0x47d3, 0x4508, {0xb5, 0x9d, 0xab, 0x1f, 0xd4, 0xfb, 0x16, 0x24}"
            "0x6ff021ed, 0x7c7b, 0x4196, {0xbc, 0x82, 0x49, 0xf0, 0x56, 0xff, 0x3c, 0xf8}" source "${source}")
        string(REPLACE "bool Runtime() {" "bool Runtime() {\n    if (!Office21048VerifyLoaded()) return false;" source "${source}")
        set(source "#include \"${CMAKE_CURRENT_LIST_DIR}/office-21048-verify.h\"\n${source}")
    endif()
    string(REPLACE "#include \"../../" "#include \"${root}/" source "${source}")
    file(GENERATE OUTPUT "${office_21048_dir}/${name}" CONTENT "${source}")
endforeach()
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
