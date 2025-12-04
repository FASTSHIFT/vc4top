project(vc4top C)
cmake_minimum_required(VERSION 3.1)
set(CMAKE_C_STANDARD 99)

add_executable(vc4top main.c)

install(TARGETS vc4top RUNTIME DESTINATION bin)

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Jonas Pfeil <pfeiljonas@gmx.de>")
set(CPACK_PACKAGE_NAME "vc4top")
set(CPACK_PACKAGE_VERSION "0.0.2")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "armhf")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Performance monitoring tool for the VC4 QPUs")
set(CPACK_PACKAGE_DESCRIPTION "Shows usage statistics of the VC4 QPU processors in the VC4 processor family. This tool needs to be run as root, as it maps the registers into its address space")
set(CPACK_DEBIAN_PACKAGE_SECTION "main")

include(CPack)
