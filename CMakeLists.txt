cmake_minimum_required(VERSION 3.16)

project(boomboxstream VERSION 0.1.0 LANGUAGES C)

find_package(libobs REQUIRED)

add_library(boomboxstream MODULE
	src/plugin-main.c
	src/boombox-filter.c)

target_compile_definitions(boomboxstream PRIVATE
	PLUGIN_VERSION="${PROJECT_VERSION}")

target_link_libraries(boomboxstream PRIVATE OBS::libobs)

if(NOT MSVC)
	target_link_libraries(boomboxstream PRIVATE m)
	target_compile_options(boomboxstream PRIVATE -Wall -Wextra)
endif()

set_target_properties(boomboxstream PROPERTIES PREFIX "")

# Layout de instalação usado pelo OBS para carregar plugins.
if(WIN32)
	set(BOOMBOX_PLUGIN_DIR "obs-plugins/64bit")
	set(BOOMBOX_DATA_DIR "data/obs-plugins/boomboxstream")
elseif(APPLE)
	set(BOOMBOX_PLUGIN_DIR "boomboxstream.plugin/Contents/MacOS")
	set(BOOMBOX_DATA_DIR "boomboxstream.plugin/Contents/Resources")
else()
	include(GNUInstallDirs)
	set(BOOMBOX_PLUGIN_DIR "${CMAKE_INSTALL_LIBDIR}/obs-plugins")
	set(BOOMBOX_DATA_DIR "${CMAKE_INSTALL_DATADIR}/obs/obs-plugins/boomboxstream")
endif()

install(TARGETS boomboxstream LIBRARY DESTINATION "${BOOMBOX_PLUGIN_DIR}"
	RUNTIME DESTINATION "${BOOMBOX_PLUGIN_DIR}")
install(DIRECTORY data/ DESTINATION "${BOOMBOX_DATA_DIR}")
