# FFmpeg's libavcodec + libavutil, built from third_party/ffmpeg with only the
# decoders AirPlay audio needs on Vega: AAC (incl. AAC-ELD, used while
# mirroring) and ALAC (used for music). LGPL configuration: no --enable-gpl,
# no --enable-nonfree. Imported static targets: rosella::avcodec, rosella::avutil.

include(ExternalProject)

get_filename_component(_rv_ff_tool_dir ${CMAKE_C_COMPILER} DIRECTORY)

if(VODKA_ARCH STREQUAL "armv7")
  set(_rv_ff_arch_flags --arch=arm --cpu=cortex-a7)
elseif(VODKA_ARCH STREQUAL "aarch64")
  set(_rv_ff_arch_flags --arch=aarch64)
elseif(VODKA_ARCH STREQUAL "x86_64")
  # No nasm in the build box; x86_64 only runs on the virtual device anyway.
  set(_rv_ff_arch_flags --arch=x86_64 --disable-x86asm)
else()
  message(FATAL_ERROR "ffmpeg.cmake: unsupported VODKA_ARCH '${VODKA_ARCH}'")
endif()

# As for OpenSSL: the SDK's clang wrapper needs the sysroot spelled out.
set(_rv_ff_cflags "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT} -fPIC")
# Configure links test programs, which needs the toolchain runtime flags too.
set(_rv_ff_ldflags "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT}")

set(_rv_ff_prefix ${CMAKE_BINARY_DIR}/ffmpeg-install)
file(MAKE_DIRECTORY ${_rv_ff_prefix}/include)

ExternalProject_Add(ffmpeg_ext
  SOURCE_DIR ${RV_THIRD_PARTY}/ffmpeg
  BINARY_DIR ${CMAKE_BINARY_DIR}/ffmpeg-build
  INSTALL_DIR ${_rv_ff_prefix}
  CONFIGURE_COMMAND
    <SOURCE_DIR>/configure
      --prefix=<INSTALL_DIR>
      --enable-cross-compile --target-os=linux ${_rv_ff_arch_flags}
      --cc=${CMAKE_C_COMPILER}
      --ar=${_rv_ff_tool_dir}/llvm-ar
      --ranlib=${_rv_ff_tool_dir}/llvm-ranlib
      --nm=${_rv_ff_tool_dir}/llvm-nm
      "--extra-cflags=${_rv_ff_cflags}"
      "--extra-ldflags=${_rv_ff_ldflags}"
      --enable-static --disable-shared --enable-pic
      --disable-everything --disable-autodetect --disable-programs --disable-doc
      --disable-avdevice --disable-avformat --disable-avfilter
      --disable-swscale --disable-swresample --disable-network --disable-iconv
      --disable-debug --disable-stripping
      --enable-decoder=aac --enable-decoder=alac
  BUILD_COMMAND make -j8
  # The Vega build exports DESTDIR, which FFmpeg's install honours; clear it so
  # the files land in INSTALL_DIR where the imported targets look.
  INSTALL_COMMAND make install DESTDIR=
  BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libavcodec.a <INSTALL_DIR>/lib/libavutil.a
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  LOG_OUTPUT_ON_FAILURE ON
)

add_library(rosella_avutil STATIC IMPORTED GLOBAL)
set_target_properties(rosella_avutil PROPERTIES
  IMPORTED_LOCATION ${_rv_ff_prefix}/lib/libavutil.a
  INTERFACE_INCLUDE_DIRECTORIES ${_rv_ff_prefix}/include
  INTERFACE_LINK_LIBRARIES "m;pthread"
)
add_dependencies(rosella_avutil ffmpeg_ext)
add_library(rosella::avutil ALIAS rosella_avutil)

add_library(rosella_avcodec STATIC IMPORTED GLOBAL)
set_target_properties(rosella_avcodec PROPERTIES
  IMPORTED_LOCATION ${_rv_ff_prefix}/lib/libavcodec.a
  INTERFACE_INCLUDE_DIRECTORIES ${_rv_ff_prefix}/include
  INTERFACE_LINK_LIBRARIES rosella_avutil
)
add_dependencies(rosella_avcodec ffmpeg_ext)
add_library(rosella::avcodec ALIAS rosella_avcodec)
