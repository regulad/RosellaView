# OpenSSL 3 (libcrypto only is linked), built from third_party/openssl with the
# SDK's clang wrapper. OpenSSL uses its own Perl-based Configure, so it runs as
# an ExternalProject; the result is the imported static target rosella::crypto.

include(ExternalProject)

get_filename_component(_rv_tool_dir ${CMAKE_C_COMPILER} DIRECTORY)

if(VODKA_ARCH STREQUAL "armv7")
  set(_rv_openssl_target linux-armv4)
elseif(VODKA_ARCH STREQUAL "aarch64")
  set(_rv_openssl_target linux-aarch64)
elseif(VODKA_ARCH STREQUAL "x86_64")
  set(_rv_openssl_target linux-x86_64)
else()
  message(FATAL_ERROR "openssl.cmake: unsupported VODKA_ARCH '${VODKA_ARCH}'")
endif()

# The SDK's clang wrapper doesn't imply the sysroot; CMake adds it from
# CMAKE_SYSROOT on its own command lines, so OpenSSL needs it spelled out along
# with the toolchain's architecture flags.
set(_rv_openssl_cflags "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT} -fPIC -O2")

set(_rv_openssl_prefix ${CMAKE_BINARY_DIR}/openssl-install)
# The imported target's include dir must exist at configure time.
file(MAKE_DIRECTORY ${_rv_openssl_prefix}/include)

ExternalProject_Add(openssl_ext
  SOURCE_DIR ${RV_THIRD_PARTY}/openssl
  BINARY_DIR ${CMAKE_BINARY_DIR}/openssl-build
  INSTALL_DIR ${_rv_openssl_prefix}
  CONFIGURE_COMMAND
    ${CMAKE_COMMAND} -E env
      CC=${CMAKE_C_COMPILER}
      AR=${_rv_tool_dir}/llvm-ar
      RANLIB=${_rv_tool_dir}/llvm-ranlib
      NM=${_rv_tool_dir}/llvm-nm
    perl <SOURCE_DIR>/Configure ${_rv_openssl_target}
      no-shared no-module no-dso no-engine no-tests no-apps no-docs
      no-ui-console no-legacy no-quic no-comp no-ssl3 no-afalgeng
      "CFLAGS=${_rv_openssl_cflags}"
      --prefix=<INSTALL_DIR> --libdir=lib --openssldir=/nonexistent
  BUILD_COMMAND make -j8 build_libs
  INSTALL_COMMAND make install_dev
  BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libcrypto.a
  LOG_CONFIGURE ON
  LOG_BUILD ON
  LOG_INSTALL ON
  LOG_OUTPUT_ON_FAILURE ON
)

add_library(rosella_crypto STATIC IMPORTED GLOBAL)
set_target_properties(rosella_crypto PROPERTIES
  IMPORTED_LOCATION ${_rv_openssl_prefix}/lib/libcrypto.a
  INTERFACE_INCLUDE_DIRECTORIES ${_rv_openssl_prefix}/include
)
add_dependencies(rosella_crypto openssl_ext)
add_library(rosella::crypto ALIAS rosella_crypto)
