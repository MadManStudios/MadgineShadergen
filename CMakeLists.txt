
cmake_minimum_required(VERSION 3.29)

PROJECT(ShaderGen)

if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  add_compile_options(-fms-extensions)
endif()

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wno-language-extension-token)
endif()

set(CMAKE_CXX_STANDARD 17)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR})

include(FetchContent)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "")
set(LLVM_APPEND_VC_REV ON CACHE BOOL "")
set(LLVM_DEFAULT_TARGET_TRIPLE "dxil-ms-dx" CACHE STRING "")
set(LLVM_ENABLE_EH ON CACHE BOOL "")
set(LLVM_ENABLE_RTTI ON CACHE BOOL "")
set(LLVM_INCLUDE_DOCS OFF CACHE BOOL "")
set(LLVM_INCLUDE_EXAMPLES OFF CACHE BOOL "")
set(LLVM_OPTIMIZED_TABLEGEN OFF CACHE BOOL "")
set(LLVM_TARGETS_TO_BUILD "None" CACHE STRING "")
set(LIBCLANG_BUILD_STATIC ON CACHE BOOL "")
set(CLANG_BUILD_EXAMPLES OFF CACHE BOOL "")
set(CLANG_CL OFF CACHE BOOL "")
set(CLANG_ENABLE_ARCMT OFF CACHE BOOL "")
set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE BOOL "")
set(HLSL_INCLUDE_TESTS ON CACHE BOOL "")
set(ENABLE_SPIRV_CODEGEN ON CACHE BOOL "")
set(SPIRV_BUILD_TESTS ON CACHE BOOL "")
set(LLVM_ENABLE_TERMINFO OFF CACHE BOOL "")

set (HLSL_BUILD_DXILCONV OFF CACHE BOOL "")
set (HLSL_COPY_GENERATED_SOURCES ON CACHE BOOL "")
set (HLSL_EMBED_VERSION ON CACHE BOOL "")


FetchContent_Declare(DirectXShaderCompiler
	GIT_REPOSITORY https://github.com/microsoft/DirectXShaderCompiler.git
	GIT_TAG v1.8.2505.1	
	)

FetchContent_MakeAvailable(DirectXShaderCompiler)

set(CMAKE_MACOSX_RPATH TRUE)
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
SET(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	SET(CMAKE_INSTALL_RPATH "@executable_path")
elseif(UNIX)
	SET(CMAKE_INSTALL_RPATH "$\{ORIGIN\}")
endif()

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)

add_subdirectory(ext/SPIRV-Cross)


add_executable(ShaderGen generateCPP.cpp main.cpp shadergenGLSL.cpp shadergenGLSLES.cpp shadergenHLSL.cpp shadergenSPIRV.cpp)

target_link_libraries(ShaderGen PRIVATE dxcompiler spirv-cross-glsl spirv-cross-hlsl spirv-cross-core)

target_include_directories(ShaderGen PRIVATE ${directxshadercompiler_SOURCE_DIR}/include ${directxshadercompiler_SOURCE_DIR}/external/DirectX-Headers/include/directx ${directxshadercompiler_SOURCE_DIR}/external/DirectX-Headers/include/wsl/stubs)

add_custom_command(TARGET ShaderGen POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dxcompiler> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

if (WIN32)
	add_custom_command(TARGET ShaderGen POST_BUILD
						COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_IMPORT_FILE:dxcompiler> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif()