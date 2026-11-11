# Verified original-tree implementations, shared with the standalone research
# contracts. A registration is only a candidate: its read-only Probe and Attach
# enforce the exact framework CodeView, native ABI, tree, and geometry profile.
set(original_framework_dir "${CMAKE_CURRENT_SOURCE_DIR}/research/framework-probe")
function(original_framework_module target source)
    set(digest "${CMAKE_SIZEOF_VOID_P}")
    set(dependencies ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/protocol.h")
    foreach(dependency IN LISTS dependencies)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dependency}")
        file(SHA256 "${dependency}" part)
        string(APPEND digest "${part}")
    endforeach()
    file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" config_hash)
    string(SHA256 digest "${digest}${config_hash}")
    string(SUBSTRING "${digest}" 0 12 digest)
    add_library(${target} SHARED "${source}")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${target}-${digest}-$<LOWER_CASE:$<CONFIG>>")
    add_dependencies(MacdowsStyleHook ${target})
endfunction()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    original_framework_module(MacdowsMfcCaption "${original_framework_dir}/mfc-paint.cpp"
        "${original_framework_dir}/mfc-paint.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/adapter-api.h" "${CMAKE_CURRENT_SOURCE_DIR}/pe-identity.h")
    target_link_libraries(MacdowsMfcCaption PRIVATE comctl32 gdi32)
    original_framework_module(MacdowsOfficeCaption "${original_framework_dir}/office.cpp"
        "${original_framework_dir}/office.cpp" "${original_framework_dir}/office-layout.h"
        "${original_framework_dir}/office-lease.h" "${original_framework_dir}/office-edit-title.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/native-style-view.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/pe-imports.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/caption-colors.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/nonclient-frame.h" "${CMAKE_CURRENT_SOURCE_DIR}/adapter-api.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/pe-identity.h")
    target_compile_definitions(MacdowsOfficeCaption PRIVATE OFFICE_LEASE_EXPERIMENT)
    target_link_libraries(MacdowsOfficeCaption PRIVATE comctl32 gdi32 dwmapi)
else()
    set(txgui_dependencies "")
    foreach(name txgui.cpp txgui-events.h txgui-caption.h txgui-tree-budget.h txgui-frame-layout.h
                 txgui-order.h txgui-stack-order.h txgui-left-layout.h txgui-left-live.h txgui-left-lease.h
                 txgui-live-state.h txgui-caption-pixels.h txgui-paint-observer.h txgui-d2d-filter.h)
        list(APPEND txgui_dependencies "${original_framework_dir}/${name}")
    endforeach()
    original_framework_module(MacdowsTxGuiCaption "${original_framework_dir}/txgui.cpp"
        ${txgui_dependencies} "${CMAKE_CURRENT_SOURCE_DIR}/adapter-api.h" "${CMAKE_CURRENT_SOURCE_DIR}/pe-identity.h")
    target_compile_definitions(MacdowsTxGuiCaption PRIVATE TXGUI_LEFT_EXPERIMENT TXGUI_LEFT_LEASE)
    target_link_libraries(MacdowsTxGuiCaption PRIVATE ole32 oleaut32 gdi32 dwmapi comctl32)
endif()
