cmake_minimum_required(VERSION 3.20)
project(fcc_sdn_plugin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include paths
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Optional: SDS FlatBuffers headers
if(DEFINED SDS_ROOT)
    include_directories(${SDS_ROOT}/packages/flatbuffers/include)
endif()

# Library sources
set(LIB_SOURCES
    src/fcc_client.cpp
)

# Plugin library
add_library(fcc_lib STATIC ${LIB_SOURCES})

# Native test binary
if(NOT EMSCRIPTEN)
    add_executable(test_fcc tests/test_fcc.cpp)
    target_link_libraries(test_fcc fcc_lib)
    enable_testing()
    add_test(NAME test_fcc COMMAND test_fcc)
endif()

# WASM build
if(EMSCRIPTEN)
    set(WASM_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../wasm/node)
    file(MAKE_DIRECTORY ${WASM_OUTPUT_DIR})

    add_executable(fcc_plugin wasm_api.cpp ${LIB_SOURCES})
    set_target_properties(fcc_plugin PROPERTIES
        OUTPUT_NAME "fcc_plugin"
        SUFFIX ".js"
        RUNTIME_OUTPUT_DIRECTORY ${WASM_OUTPUT_DIR}
    )
    target_link_options(fcc_plugin PRIVATE
        --bind
        -s MODULARIZE=1
        -s EXPORT_NAME='createFCCPlugin'
        -s EXPORTED_FUNCTIONS=['_parse','_convert','_poll_filings','_check_alerts','_wasm_malloc','_wasm_free']
        -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString']
        -s ALLOW_MEMORY_GROWTH=1
        -s TOTAL_MEMORY=16777216
        -s WASM=1
        -O2
    )
endif()
