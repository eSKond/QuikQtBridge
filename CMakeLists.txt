cmake_minimum_required(VERSION 3.14)
set (QuikQtPluginLib "QuikQtBridge")
string(TOUPPER ${QuikQtPluginLib} UCASELIBNAME)

project(${QuikQtPluginLib} LANGUAGES CXX)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "/MT")
set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Zi")

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Network REQUIRED)

configure_file(
    quikcoastbootstrap.h.in
    generated/quikcoastbootstrap.h
    @ONLY
    )

configure_file(
    quikcoastbootstrap.cpp.in
    generated/quikcoastbootstrap.cpp
    @ONLY
    )

qt5_wrap_cpp(moc_files bridgetcpserver.h jsonprotocolhandler.h)

add_library(${QuikQtPluginLib} SHARED
  QuikQtBridge_global.h
  quikqtbridge.cpp
  quikqtbridge.h
  quikcoast.h
  quikcoast.cpp
  qtmain.cpp
  bridgetcpserver.cpp
  jsonprotocolhandler.cpp
  serverconfigreader.h
  serverconfigreader.cpp
  ${moc_files}
  ${CMAKE_CURRENT_BINARY_DIR}/generated/quikcoastbootstrap.cpp
)

target_link_libraries(${QuikQtPluginLib} PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_link_libraries(${QuikQtPluginLib} PRIVATE Qt${QT_VERSION_MAJOR}::Network)

target_compile_definitions(${QuikQtPluginLib} PRIVATE ${UCASELIBNAME}_LIBRARY)
target_compile_definitions(${QuikQtPluginLib} PRIVATE QUIK_QT_PLUGIN_LIB_NAME=${QuikQtPluginLib})

target_include_directories(${QuikQtPluginLib} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
#target_include_directories(${QuikQtPluginLib} PRIVATE "c:/Work/lua-5.3.6_Win64_dll16_lib/include")
#target_link_directories(${QuikQtPluginLib} PRIVATE "c:/Work/lua-5.3.6_Win64_dll16_lib/fromquik")
#target_link_libraries(${QuikQtPluginLib} PRIVATE lua53)
target_include_directories(${QuikQtPluginLib} PRIVATE "D:/Work/Trade/lua-5.4.2-win64-libs/include")
target_link_directories(${QuikQtPluginLib} PRIVATE "D:/Work/Trade/lua-5.4.2-win64-libs/fromquik")
target_link_libraries(${QuikQtPluginLib} PRIVATE lua54)
