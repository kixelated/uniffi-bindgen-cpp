cmake_minimum_required(VERSION 3.10)
project(binding-tests VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# The C backend's header promises C99.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/permissive-)
else()
    add_compile_options(-pedantic-errors)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Generated files must end with a newline; consumers build with this as an error.
    add_compile_options(-Werror=newline-eof)
endif()

include(CTest)
enable_testing()

find_package(Threads REQUIRED)

set(BINDINGS_BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../target/debug)
set(BINDINGS_SRC_DIR ${BINDINGS_BUILD_DIR}/bindings)

if (WIN32)
    set(UNIFFI_FIXTURES_LIB uniffi_bindgen_cpp_fixtures.dll)
    set(ADDITIONAL_LIBS ws2_32 userenv advapi32 ntdll crypt32 Bcrypt)
elseif(UNIX AND NOT APPLE)
    set(UNIFFI_FIXTURES_LIB libuniffi_bindgen_cpp_fixtures.so)
    set(ADDITIONAL_LIBS)
else()
    set(UNIFFI_FIXTURES_LIB libuniffi_bindgen_cpp_fixtures.dylib)
    set(ADDITIONAL_LIBS)
endif()

find_program(VALGRIND "valgrind")
macro(memcheck_test TEST_NAME)
if (VALGRIND)
    add_test(NAME ${TEST_NAME}-test-memcheck
    COMMAND valgrind
        --error-exitcode=1
        --tool=memcheck
        --leak-check=full
        --errors-for-leak-kinds=definite
        --show-leak-kinds=definite $<TARGET_FILE:${TEST_NAME}>)
endif()
endmacro(memcheck_test)

# Add a bindings test case
macro(test_case TEST_NAME)
add_executable(${TEST_NAME}-test tests/${TEST_NAME}/main.cpp ${BINDINGS_SRC_DIR}/${TEST_NAME}.cpp)

target_include_directories(${TEST_NAME}-test PRIVATE ${BINDINGS_SRC_DIR} include)
target_link_directories(${TEST_NAME}-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(${TEST_NAME}-test uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
target_compile_definitions(${TEST_NAME}-test PRIVATE UNIFFI_BINDING_DIR="${BINDINGS_SRC_DIR}")

add_test(NAME ${TEST_NAME}-test COMMAND ${TEST_NAME}-test)
memcheck_test(${TEST_NAME}-test)

add_dependencies(${TEST_NAME}-test bindings)

list(APPEND BINDING_FILES ${BINDINGS_SRC_DIR}/${TEST_NAME}.cpp)

endmacro(test_case)

test_case(arithmetic)
test_case(callbacks)
test_case(fixture_callbacks)
test_case(chronological)
test_case(custom_types)
test_case(geometry)
test_case(rondpoint)
test_case(sprites)
test_case(todolist)
test_case(traits)
test_case(coverall)
test_case(uniffi_docstring)
test_case(trait_methods)
test_case(custom_types_builtin)
test_case(enum_style_test)
test_case(empty_type)
test_case(error_types_builtin)
test_case(reserved_field_name)
test_case(type_flattening)
test_case(uniffi_bindgen_cpp_fixtures)
test_case(futures)

add_executable(async-dispatcher-test
    tests/async_dispatcher/main.cpp
    ${BINDINGS_SRC_DIR}/futures.cpp)
target_include_directories(async-dispatcher-test PRIVATE ${BINDINGS_SRC_DIR} include)
target_link_directories(async-dispatcher-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(async-dispatcher-test
    uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
add_dependencies(async-dispatcher-test bindings)

foreach(SCENARIO IN ITEMS inline rejection lifetime)
    add_test(NAME async-dispatcher-${SCENARIO}-test
        COMMAND async-dispatcher-test ${SCENARIO})
    set_tests_properties(async-dispatcher-${SCENARIO}-test PROPERTIES TIMEOUT 15)
endforeach()

add_executable(ext_types_import-test
    tests/ext_types_import/main.cpp
    ${BINDINGS_SRC_DIR}/ext_types_export.cpp
    ${BINDINGS_SRC_DIR}/ext_types_import.cpp)
target_include_directories(ext_types_import-test PRIVATE ${BINDINGS_SRC_DIR} include)
target_link_directories(ext_types_import-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(ext_types_import-test uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
add_test(NAME ext_types_import-test COMMAND ext_types_import-test)
memcheck_test(ext_types_import-test)
add_dependencies(ext_types_import-test bindings)
list(APPEND BINDING_FILES
    ${BINDINGS_SRC_DIR}/ext_types_export.cpp
    ${BINDINGS_SRC_DIR}/ext_types_import.cpp)

# `error_style = "expected"` bindings, built the way Unreal builds: no exceptions, no RTTI.
set(EXPECTED_BINDINGS_DIR ${BINDINGS_BUILD_DIR}/bindings-expected)
add_executable(expected-test
    tests/expected/main.cpp
    ${EXPECTED_BINDINGS_DIR}/futures.cpp
    ${EXPECTED_BINDINGS_DIR}/error_types_builtin.cpp
    ${EXPECTED_BINDINGS_DIR}/trait_methods.cpp)
target_include_directories(expected-test PRIVATE ${EXPECTED_BINDINGS_DIR})
target_link_directories(expected-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(expected-test uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
if (MSVC)
    target_compile_options(expected-test PRIVATE /EHs-c- /GR-)
    target_compile_definitions(expected-test PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(expected-test PRIVATE -fno-exceptions -fno-rtti)
endif()
add_dependencies(expected-test bindings-expected)
add_test(NAME expected-test COMMAND expected-test)
set_tests_properties(expected-test PROPERTIES TIMEOUT 30)
memcheck_test(expected-test)
list(APPEND EXPECTED_BINDING_FILES
    ${EXPECTED_BINDINGS_DIR}/futures.cpp
    ${EXPECTED_BINDINGS_DIR}/error_types_builtin.cpp
    ${EXPECTED_BINDINGS_DIR}/trait_methods.cpp)

# The C backend: a C99 header over expected-style C++ bindings, built without exceptions.
set(C_BINDINGS_DIR ${BINDINGS_BUILD_DIR}/bindings-c)
set(C_BINDING_FILES ${C_BINDINGS_DIR}/capi.cpp ${C_BINDINGS_DIR}/capi_c.cpp)
macro(c_test_case TEST_NAME)
add_executable(${TEST_NAME}-test
    tests/${TEST_NAME}/main.c
    tests/c_support/support.cpp
    ${C_BINDING_FILES})
target_include_directories(${TEST_NAME}-test PRIVATE ${C_BINDINGS_DIR} include)
target_link_directories(${TEST_NAME}-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(${TEST_NAME}-test uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
if (MSVC)
    target_compile_options(${TEST_NAME}-test PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/EHs-c- /GR->)
    target_compile_definitions(${TEST_NAME}-test PRIVATE $<$<COMPILE_LANGUAGE:CXX>:_HAS_EXCEPTIONS=0>)
else()
    target_compile_options(${TEST_NAME}-test PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Werror>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endif()
add_dependencies(${TEST_NAME}-test bindings-c)
add_test(NAME ${TEST_NAME}-test COMMAND ${TEST_NAME}-test)
set_tests_properties(${TEST_NAME}-test PROPERTIES TIMEOUT 30)
memcheck_test(${TEST_NAME}-test)
endmacro(c_test_case)

c_test_case(c_api)
c_test_case(c_dispatcher)

# The C backend refuses what it cannot render instead of emitting half of it.
add_test(NAME c-refuses-callbacks-test
    COMMAND ${BINDINGS_BUILD_DIR}/uniffi-bindgen-cpp${CMAKE_EXECUTABLE_SUFFIX}
        --lang c --library ${UNIFFI_FIXTURES_LIB} --crate uniffi_callbacks
        --out-dir ${CMAKE_CURRENT_BINARY_DIR}/c-refused
    WORKING_DIRECTORY ${BINDINGS_BUILD_DIR})
set_tests_properties(c-refuses-callbacks-test PROPERTIES
    PASS_REGULAR_EXPRESSION "the C backend does not support callback interfaces")

# Special multilib test that only needs to compile to check for potential symbol clashes
add_executable(multilib-test 
    tests/multilib/main.cpp
    ${BINDINGS_SRC_DIR}/arithmetic.cpp
    ${BINDINGS_SRC_DIR}/coverall.cpp
    ${BINDINGS_SRC_DIR}/sprites.cpp
)
target_include_directories(multilib-test PRIVATE ${BINDINGS_SRC_DIR} include)
target_link_directories(multilib-test PRIVATE ${BINDINGS_BUILD_DIR})
target_link_libraries(multilib-test uniffi_bindgen_cpp_fixtures Threads::Threads ${ADDITIONAL_LIBS})
add_dependencies(multilib-test bindings)

add_custom_target(libs ALL
    BYPRODUCTS ${BINDING_FILES}
    COMMAND cargo build
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../
    COMMENT "Generating libs"
)

add_custom_target(bindings ALL
    DEPENDS libs
    COMMAND ./uniffi-bindgen-cpp --library ${UNIFFI_FIXTURES_LIB} --out-dir bindings
    WORKING_DIRECTORY ${BINDINGS_BUILD_DIR}
    COMMENT "Generating bindings"
)

add_custom_target(bindings-c ALL
    DEPENDS libs
    BYPRODUCTS ${C_BINDING_FILES}
    COMMAND ./uniffi-bindgen-cpp --lang c --library ${UNIFFI_FIXTURES_LIB} --crate uniffi_c_api
        --out-dir bindings-c
    WORKING_DIRECTORY ${BINDINGS_BUILD_DIR}
    COMMENT "Generating C bindings"
)

# The expected style refuses callback interfaces, so only generate crates without them.
add_custom_target(bindings-expected ALL
    DEPENDS libs
    BYPRODUCTS ${EXPECTED_BINDING_FILES}
    COMMAND ./uniffi-bindgen-cpp --library ${UNIFFI_FIXTURES_LIB} --crate uniffi_fixture_async_calls
        --config ${CMAKE_CURRENT_SOURCE_DIR}/expected.toml --out-dir bindings-expected
    COMMAND ./uniffi-bindgen-cpp --library ${UNIFFI_FIXTURES_LIB} --crate uniffi_cpp_error_types_builtin
        --config ${CMAKE_CURRENT_SOURCE_DIR}/expected.toml --out-dir bindings-expected
    COMMAND ./uniffi-bindgen-cpp --library ${UNIFFI_FIXTURES_LIB} --crate uniffi_trait_methods
        --config ${CMAKE_CURRENT_SOURCE_DIR}/expected.toml --out-dir bindings-expected
    WORKING_DIRECTORY ${BINDINGS_BUILD_DIR}
    COMMENT "Generating expected-style bindings"
)
