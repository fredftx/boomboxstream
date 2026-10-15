cmake_minimum_required(VERSION 3.16)

project(boomboxstream VERSION 0.2.0 LANGUAGES C)

# Caminho normal: libobs instalado ou um build do OBS (libobsConfig.cmake).
# Alternativa sem compilar o OBS: headers do código-fonte + import libs geradas
# a partir das DLLs de uma instalação (é o que o CI do Windows faz):
#   -DLIBOBS_INCLUDE_DIR=.../obs-studio/libobs -DLIBOBS_LIB=.../obs.lib
#   -DW32_PTHREADS_INCLUDE_DIR=.../deps/w32-pthreads -DW32_PTHREADS_LIB=.../w32-pthreads.lib
if(LIBOBS_INCLUDE_DIR AND LIBOBS_LIB)
	add_library(OBS::libobs INTERFACE IMPORTED)
	set_target_properties(OBS::libobs PROPERTIES
		INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR};${W32_PTHREADS_INCLUDE_DIR}"
		INTERFACE_LINK_LIBRARIES "${LIBOBS_LIB};${W32_PTHREADS_LIB}")
else()
	find_package(libobs REQUIRED)
endif()

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
