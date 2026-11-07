# sentry-native (MIT), for native crash reports. The inproc backend unwinds the
# crashed thread inside the process (with the vendored libunwind) and sends a
# stack trace that Sentry symbolicates with the uploaded debug files. Breakpad
# produced invalid minidumps on Vega (its dump writer ptraces the crashed
# process from a child, which the app sandbox doesn't allow), and crashpad
# needs a separate handler process. No built-in transport: envelopes
# are handed to JS, which posts them (src/sentry.c), so no HTTP stack is needed
# natively. Crashes are written to the database directory and sent on the next
# launch.
set(SENTRY_BACKEND inproc CACHE STRING "" FORCE)
set(SENTRY_TRANSPORT none CACHE STRING "" FORCE)
set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SENTRY_PIC ON CACHE BOOL "" FORCE)

enable_language(CXX)
# sentry-native's vendored libunwind has assembly sources; clang assembles them.
set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
enable_language(ASM)
add_subdirectory(${RV_THIRD_PARTY}/sentry-native ${CMAKE_BINARY_DIR}/sentry-native EXCLUDE_FROM_ALL)
