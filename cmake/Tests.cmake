option(ASTRA_BUILD_TESTS "Build compiler/VM round-trip tests and fixture compiler" ON)
if(NOT ASTRA_BUILD_TESTS)
    return()
endif()

set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ASTRA_LUAU_DIR}" "luau" EXCLUDE_FROM_ALL)
enable_testing()

set(ASTRA_TEST_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dev/tests")

function(astra_add_vm_test Target Source Name)
    add_executable(${Target} "${ASTRA_TEST_DIRECTORY}/${Source}")
    target_link_libraries(${Target} PRIVATE AstraDecompiler Luau.Compiler Luau.VM)
    target_include_directories(${Target} PRIVATE
        ${ASTRA_INTERNAL_INCLUDE_DIRS}
        "${ASTRA_LUAU_DIR}/Common/include")
    add_test(NAME ${Name} COMMAND ${Target})
endfunction()

astra_add_vm_test(astra-tests Tests.cpp RoundTrip)
target_compile_definitions(astra-tests PRIVATE ASTRA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

astra_add_vm_test(astra-pipeline-tests PipelineTests.cpp Pipeline)
astra_add_vm_test(astra-control-flow-tests ControlFlowStress.cpp ControlFlowStress)
set_tests_properties(ControlFlowStress PROPERTIES TIMEOUT 120)

astra_add_vm_test(astra-inline-recovery-tests InlineRecoveryTests.cpp InlineRecovery)
set_tests_properties(InlineRecovery PROPERTIES TIMEOUT 30)

astra_add_vm_test(astra-core-validation-tests CoreValidationTests.cpp CoreValidation)
set_tests_properties(CoreValidation PROPERTIES TIMEOUT 30)

add_executable(astra-compile dev/tools/Compile.cpp)
target_link_libraries(astra-compile PRIVATE Luau.Compiler Luau.VM)

if(WIN32)
    find_program(ASTRA_POWERSHELL NAMES pwsh powershell)
    if(ASTRA_POWERSHELL)
        add_test(NAME WebSocketBridge COMMAND "${ASTRA_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${ASTRA_TEST_DIRECTORY}/WebSocket.ps1"
            -Executable $<TARGET_FILE:astra> -Compiler $<TARGET_FILE:astra-compile>
            -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}/WebSocketTests")
        set_tests_properties(WebSocketBridge PROPERTIES TIMEOUT 60)

        add_test(NAME BatchCLI COMMAND "${ASTRA_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
            -File "${ASTRA_TEST_DIRECTORY}/Batch.ps1"
            -Executable $<TARGET_FILE:astra> -Compiler $<TARGET_FILE:astra-compile>
            -WorkDirectory "${CMAKE_CURRENT_BINARY_DIR}/BatchTests")
        set_tests_properties(BatchCLI PROPERTIES TIMEOUT 60)
    endif()
endif()
