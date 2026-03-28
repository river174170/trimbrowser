# cmake/aarch64-trimui.cmake
# Cross-compilation toolchain for TrimUI Smart Pro (Allwinner A133P, aarch64)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar   CACHE FILEPATH "AR")
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib CACHE FILEPATH "RANLIB")
set(CMAKE_STRIP        aarch64-linux-gnu-strip)

# Sysroot — TrimUI SDK_usr_tg5040_a133p.tgz extracted here
set(TRIMUI_SYSROOT "/opt/trimui-sdk/sysroot" CACHE PATH "TrimUI sysroot")
set(CMAKE_SYSROOT ${TRIMUI_SYSROOT})

# Where to find libraries and headers
set(CMAKE_FIND_ROOT_PATH
    ${TRIMUI_SYSROOT}
    /tmp/curl-install      # static curl install prefix
)

# Search behavior
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)   # use host executables
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)    # target libraries only
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)    # target headers only
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Compiler flags for A133P (Cortex-A53)
set(CMAKE_C_FLAGS_INIT   "-march=armv8-a+crc -mtune=cortex-a53")
set(CMAKE_CXX_FLAGS_INIT "-march=armv8-a+crc -mtune=cortex-a53")
