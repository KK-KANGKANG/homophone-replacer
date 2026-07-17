cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(homophone-replacer-standalone)

find_package(Python3 COMPONENTS Interpreter)

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
include(${CMAKE_SOURCE_DIR}/cmake/http-service-deps.cmake)
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

# 创建可复用核心库
add_library(homophone-replacer-core STATIC
  ${UTILS_SOURCES}
  ${CORE_SOURCES}
)
target_link_libraries(homophone-replacer-core
  PUBLIC kaldifst_core fst
)
target_include_directories(homophone-replacer-core
  PUBLIC ${PROJECT_SOURCE_DIR}/src
  SYSTEM PUBLIC $<TARGET_PROPERTY:fst,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_features(homophone-replacer-core PUBLIC cxx_std_17)
if(NOT WIN32)
  target_link_libraries(homophone-replacer-core PUBLIC pthread)
endif()

add_library(homophone-replacer-service-core STATIC
  src/server/service-config.cc
  src/server/text-replacer.cc
  src/server/replacer-pool.cc
  src/server/service-api.cc
)
target_include_directories(homophone-replacer-service-core
  PUBLIC ${PROJECT_SOURCE_DIR}/src
)
target_compile_features(homophone-replacer-service-core PUBLIC cxx_std_17)
target_link_libraries(homophone-replacer-service-core
  PUBLIC
    homophone-replacer-core
    nlohmann_json::nlohmann_json
    spdlog::spdlog_header_only
)

add_executable(homophone-replacer-server
  src/server/main.cc
  src/server/http-server.cc
  src/server/service-runner.cc
)
if(WIN32)
  target_sources(homophone-replacer-server PRIVATE src/server/windows-service.cc)
endif()
target_compile_features(homophone-replacer-server PRIVATE cxx_std_17)
target_link_libraries(homophone-replacer-server
  PRIVATE
    homophone-replacer-service-core
    httplib::httplib
)

# 创建命令行程序
add_executable(homophone-replacer-standalone
  ${MAIN_SOURCES}
)

# 链接库
target_link_libraries(homophone-replacer-standalone
  PRIVATE homophone-replacer-core
)

# 继承 openfst 的头文件搜索路径，确保能找到 fst/fst.h
target_include_directories(homophone-replacer-standalone
  SYSTEM PRIVATE
    $<TARGET_PROPERTY:fst,INTERFACE_INCLUDE_DIRECTORIES>
)

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

  add_executable(service-config-test
    tests/service-config-test.cc
  )
  target_include_directories(service-config-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(service-config-test PRIVATE cxx_std_17)
  target_link_libraries(service-config-test PRIVATE homophone-replacer-service-core)
  add_test(NAME service-config COMMAND service-config-test)

  add_executable(replacer-pool-test
    tests/replacer-pool-test.cc
  )
  target_include_directories(replacer-pool-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(replacer-pool-test PRIVATE cxx_std_17)
  target_link_libraries(replacer-pool-test PRIVATE homophone-replacer-service-core)
  add_test(NAME replacer-pool COMMAND replacer-pool-test)

  add_executable(service-api-test tests/service-api-test.cc)
  target_include_directories(service-api-test PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(service-api-test PRIVATE cxx_std_17)
  target_link_libraries(service-api-test PRIVATE homophone-replacer-service-core)
  add_test(NAME service-api COMMAND service-api-test)

  if(Python3_Interpreter_FOUND)
    add_test(NAME http-server-smoke
      COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/http-server-smoke-test.py
        --server $<TARGET_FILE:homophone-replacer-server>
        --root ${PROJECT_SOURCE_DIR}
    )
  endif()
endif()

# 安装规则
install(TARGETS homophone-replacer-standalone homophone-replacer-server
  RUNTIME DESTINATION bin
)
install(FILES config/service.json DESTINATION config)
install(FILES
  data/hr-files/lexicon.txt
  data/hr-files/replace.fst
  data/hr-files/mapping.txt
  DESTINATION data/hr-files
)
install(PROGRAMS
  scripts/run-server.sh
  scripts/install-service.sh
  scripts/uninstall-service.sh
  DESTINATION scripts
)
install(FILES
  scripts/run-server.bat
  scripts/install-service.bat
  scripts/uninstall-service.bat
  scripts/start-service.bat
  scripts/stop-service.bat
  DESTINATION scripts
)
install(FILES packaging/homophone-replacer.service DESTINATION packaging)

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
