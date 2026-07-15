cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(homophone-replacer-standalone)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
  message(STATUS "No CMAKE_BUILD_TYPE given, default to Release")
  set(CMAKE_BUILD_TYPE Release)
endif()

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 编译器特定设置
if(WIN32 AND MSVC)
  # MSVC静态链接运行时库
  add_compile_options(
      $<$<CONFIG:>:/MT>
      $<$<CONFIG:Debug>:/MTd>
      $<$<CONFIG:Release>:/MT>
      $<$<CONFIG:RelWithDebInfo>:/MT>
      $<$<CONFIG:MinSizeRel>:/MT>
  )
  
  # 禁用一些MSVC警告
  add_compile_options(/wd4244 /wd4267 /wd4996)
  
  # 设置UTF-8编码
  add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
  add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
  
  # 定义NOMINMAX防止Windows.h定义min/max宏
  add_definitions(-DNOMINMAX)
endif()

# Linux/GCC设置
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
endif()

# 包含目录
include_directories(${PROJECT_SOURCE_DIR}/src)

# 引入 kaldifst (提供 TextNormalizer)
include(${CMAKE_SOURCE_DIR}/cmake/kaldifst.cmake)
include_directories(SYSTEM ${kaldifst_SOURCE_DIR})
# openfst 头文件目录（与主库一致）：${openfst_SOURCE_DIR}/src/include
if(DEFINED openfst_SOURCE_DIR)
  include_directories(SYSTEM ${openfst_SOURCE_DIR}/src/include)
endif()

# 源文件
set(UTILS_SOURCES
  src/utils/file-utils.cc
  src/utils/text-utils.cc
)

set(CORE_SOURCES
  src/homophone-replacer.cc
  src/runtime-rule-matcher.cc
)

set(MAIN_SOURCES
  src/main.cc
)

# 创建可执行文件
add_executable(homophone-replacer-standalone
  ${UTILS_SOURCES}
  ${CORE_SOURCES}
  ${MAIN_SOURCES}
)

# 链接库
target_link_libraries(homophone-replacer-standalone
  PRIVATE
    kaldifst_core
    fst
)

# 继承 openfst 的头文件搜索路径，确保能找到 fst/fst.h
target_include_directories(homophone-replacer-standalone
  SYSTEM PRIVATE
    $<TARGET_PROPERTY:fst,INTERFACE_INCLUDE_DIRECTORIES>
)

if(NOT WIN32)
  target_link_libraries(homophone-replacer-standalone PRIVATE pthread)
endif()

# 设置编译器特性
target_compile_features(homophone-replacer-standalone PRIVATE cxx_std_17)

include(CTest)
if(BUILD_TESTING)
  add_executable(runtime-rule-matcher-test
    tests/runtime-rule-matcher-test.cc
    src/runtime-rule-matcher.cc
  )
  target_include_directories(runtime-rule-matcher-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(runtime-rule-matcher-test PRIVATE cxx_std_17)
  add_test(NAME runtime-rule-matcher COMMAND runtime-rule-matcher-test)
endif()

# 安装规则
install(TARGETS homophone-replacer-standalone
  RUNTIME DESTINATION bin
)

# 打印配置信息
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_FLAGS: ${CMAKE_CXX_FLAGS}")

# 提示信息
message(STATUS "")
message(STATUS "Build configuration complete!")
message(STATUS "To build:")
if(WIN32)
  message(STATUS "  - On Windows: Open project in Visual Studio and build, or use 'cmake --build .'")
else()
  message(STATUS "  - On Linux: Run 'make' or 'cmake --build .'")
endif()
message(STATUS "")
