
if(WIN32)
find_package(TAEF REQUIRED)
include_directories(${TAEF_INCLUDE_DIRS})

add_clang_library(HLSLTestLib
  D3DReflectionDumper.cpp
  DxcTestUtils.cpp
  HlslTestUtils.cpp
  FileCheckerTest.cpp
  FileCheckForTest.cpp
  RDATDumper.cpp
)
else(WIN32)
set(HLSL_IGNORE_SOURCES
  D3DReflectionDumper.cpp
  RDATDumper.cpp
)
add_clang_library(HLSLTestLib
  DxcTestUtils.cpp
  HlslTestUtils.cpp
  FileCheckerTest.cpp
  FileCheckForTest.cpp
)
include_directories(${LLVM_MAIN_SRC_DIR}/utils/unittest/googletest/include)
include_directories(${LLVM_MAIN_SRC_DIR}/utils/unittest/googlemock/include)

endif(WIN32)

add_dependencies(HLSLTestLib TablegenHLSLOptions)

set_target_properties(HLSLTestLib PROPERTIES FOLDER "Clang tests")
