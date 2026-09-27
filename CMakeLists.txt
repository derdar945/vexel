cmake_minimum_required(VERSION 3.15)
project(vexel VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (NOT WIN32)
    message(WARNING "Vexel is Windows-first: core builds, Win32 operators/backends are skipped (see PORTING.md)")
endif()

add_executable(vexel
    src/main.c
    src/vexel.c
    src/runtime.c
    src/lexer.c
    src/parser.c
    src/compiler.c
    src/vm.c
    src/fmt.c
    src/lint.c
    src/vtest.c
    src/dbg.c
    src/lsp.c
    src/opman.c
    src/opnew.c
    src/oploader.c
)

target_include_directories(vexel PRIVATE include)

if (WIN32)
    target_link_libraries(vexel PRIVATE wininet)
endif()
if (MSVC)
    target_compile_options(vexel PRIVATE /W3)
else()
    target_compile_options(vexel PRIVATE -Wall -Wextra)
    target_link_libraries(vexel PRIVATE m)
endif()

# ---------- first-party native operators (Windows only, see PORTING.md)
if (WIN32)
add_library(VexGUI SHARED operators/VexGUI/src/vgui.c)
target_include_directories(VexGUI PRIVATE include)
set_target_properties(VexGUI PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/VexGUI"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/VexGUI"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/VexGUI"
    PREFIX ""
)
if (MSVC)
    target_compile_options(VexGUI PRIVATE /W3)
else()
    target_compile_options(VexGUI PRIVATE -Wall -Wextra)
endif()
if (WIN32)
    target_link_libraries(VexGUI PRIVATE user32 gdi32)
endif()

# ---------- more first-party native operators ---------------------------
function(add_vxop OP SRC)
    add_library(${OP} SHARED ${SRC})
    target_include_directories(${OP} PRIVATE include)
    set_target_properties(${OP} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/${OP}"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/${OP}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/operators/${OP}"
        PREFIX ""
    )
    if (MSVC)
        target_compile_options(${OP} PRIVATE /W3)
    else()
        target_compile_options(${OP} PRIVATE -Wall -Wextra)
    endif()
    if (WIN32)
        target_link_libraries(${OP} PRIVATE user32 gdi32)
    endif()
endfunction()

add_vxop(VexFS operators/VexFS/src/vfs.c)
add_vxop(VexExec operators/VexExec/src/vexec.c)
add_vxop(VexNet operators/VexNet/src/vnet.c)
if (WIN32)
    target_link_libraries(VexNet PRIVATE ws2_32)
endif()
add_vxop(VexGame operators/VexGame/src/vgame.c)
if (WIN32)
    target_link_libraries(VexGame PRIVATE gdi32 user32)
endif()
add_vxop(Vex3D operators/Vex3D/src/v3d.c)
if (WIN32)
    target_link_libraries(Vex3D PRIVATE gdi32 user32)
endif()
add_vxop(VexSYS operators/VexSYS/src/vsys.c)
if (WIN32)
    target_link_libraries(VexSYS PRIVATE shell32 advapi32)
endif()
add_vxop(VexGL operators/VexGL/src/vgl.c)
if (WIN32)
    target_link_libraries(VexGL PRIVATE opengl32)
endif()
endif() # WIN32 native operators
