cmake_minimum_required(VERSION 3.16)

project(qmletherui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include directories
find_path(ETHERCAT_INCLUDE_DIR ecrt.h
        PATHS /opt/etherlab/include
        PATH_SUFFIXES ethercat
        REQUIRED
)


find_library(ETHERCAT_MASTER_LIBRARY
    NAMES ethercat
    PATHS /opt/etherlab/lib /usr/local/lib /usr/lib
    REQUIRED
)
# Add include directories
include_directories(${ETHERCAT_INCLUDE_DIR})

find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appqmletherui
    main.cpp
)

qt_add_qml_module(appqmletherui
    URI qmletherui
    VERSION 1.0
    QML_FILES
        Main.qml
        SOURCES ethercatcontroller.h ethercatcontroller.cpp
)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appqmletherui PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appqmletherui
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appqmletherui
    PRIVATE
    Qt6::Quick
    Qt6::Core
    ${ETHERCAT_MASTER_LIBRARY}
    rt
    pthread
)

target_include_directories(appqmletherui PRIVATE ${ETHERCAT_INCLUDE_DIR})

include(GNUInstallDirs)
install(TARGETS appqmletherui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
