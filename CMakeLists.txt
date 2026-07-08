cmake_minimum_required(VERSION 3.16)

project(thor_load_scope
  VERSION 0.1.0
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(tlscope_core
  src/aggregate.cpp
  src/csv_writer.cpp
  src/proc_reader.cpp
  src/sample_engine.cpp
  src/target_resolver.cpp
)

target_include_directories(tlscope_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(tlscope_core
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(tlscope_core
  PUBLIC
    Threads::Threads
)

add_executable(thor-load-scope
  src/main.cpp
)

target_compile_options(thor-load-scope
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(thor-load-scope
  PRIVATE
    tlscope_core
)

add_executable(tlscope-periodic-load
  tests/fixtures/periodic_load.cpp
)

target_compile_options(tlscope-periodic-load
  PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(tlscope-periodic-load
  PRIVATE
    Threads::Threads
)

install(TARGETS thor-load-scope tlscope-periodic-load
  RUNTIME DESTINATION bin
)

option(TLSCOPE_BUILD_QT "Build the optional Qt Widgets viewer when Qt is available" ON)

if(TLSCOPE_BUILD_QT)
  find_package(Qt6 COMPONENTS Widgets QUIET)
  if(Qt6_FOUND)
    add_executable(thor-load-scope-qt
      src/qt/qt_main.cpp
    )
    target_compile_options(thor-load-scope-qt
      PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
    target_link_libraries(thor-load-scope-qt
      PRIVATE
        tlscope_core
        Qt6::Widgets
    )
    install(TARGETS thor-load-scope-qt RUNTIME DESTINATION bin)
  else()
    find_package(Qt5 COMPONENTS Widgets QUIET)
    if(Qt5_FOUND)
      add_executable(thor-load-scope-qt
        src/qt/qt_main.cpp
      )
      target_compile_options(thor-load-scope-qt
        PRIVATE
          -Wall
          -Wextra
          -Wpedantic
      )
      target_link_libraries(thor-load-scope-qt
        PRIVATE
          tlscope_core
          Qt5::Widgets
      )
      install(TARGETS thor-load-scope-qt RUNTIME DESTINATION bin)
    else()
      message(STATUS "Qt Widgets not found; building CLI targets only")
    endif()
  endif()
endif()
