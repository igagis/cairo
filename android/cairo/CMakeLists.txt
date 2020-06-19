# Sets the minimum version of CMake required to build your native library.
# This ensures that a certain set of CMake features is available to
# your build.

cmake_minimum_required(VERSION 3.4.1)

# Specifies a library name, specifies whether the library is STATIC or
# SHARED, and provides relative paths to the source code. You can
# define multiple libraries by adding multiple add_library() commands,
# and CMake builds them for you. When you build your app, Gradle
# automatically packages shared libraries with your APK.

set(name cairo)

# file(GLOB_RECURSE srcs "../../src/*.cpp")

set(srcs
        ../../src/cairo-analysis-surface.c
        ../../src/cairo-arc.c
        ../../src/cairo-array.c
        ../../src/cairo-atomic.c
        ../../src/cairo-base64-stream.c
        ../../src/cairo-base85-stream.c
        ../../src/cairo-bentley-ottmann.c
        ../../src/cairo-bentley-ottmann-rectangular.c
        ../../src/cairo-bentley-ottmann-rectilinear.c
        ../../src/cairo-botor-scan-converter.c
        ../../src/cairo-boxes.c
        ../../src/cairo-boxes-intersect.c
        ../../src/cairo.c
        ../../src/cairo-cache.c
        ../../src/cairo-clip.c
        ../../src/cairo-clip-boxes.c
        ../../src/cairo-clip-polygon.c
        ../../src/cairo-clip-region.c
        ../../src/cairo-clip-surface.c
        ../../src/cairo-clip-tor-scan-converter.c
        ../../src/cairo-color.c
        ../../src/cairo-composite-rectangles.c
        ../../src/cairo-compositor.c
        ../../src/cairo-contour.c
        ../../src/cairo-damage.c
        ../../src/cairo-debug.c
        ../../src/cairo-default-context.c
        ../../src/cairo-device.c
        ../../src/cairo-error.c
        ../../src/cairo-fallback-compositor.c
        ../../src/cairo-fixed.c
        ../../src/cairo-font-face.c
        ../../src/cairo-font-face-twin.c
        ../../src/cairo-font-face-twin-data.c
        ../../src/cairo-font-options.c
        ../../src/cairo-freelist.c
        ../../src/cairo-freed-pool.c
        ../../src/cairo-gstate.c
        ../../src/cairo-hash.c
        ../../src/cairo-hull.c
        ../../src/cairo-image-compositor.c
        ../../src/cairo-image-info.c
        ../../src/cairo-image-source.c
        ../../src/cairo-image-surface.c
        ../../src/cairo-line.c
        ../../src/cairo-lzw.c
        ../../src/cairo-matrix.c
        ../../src/cairo-mask-compositor.c
        ../../src/cairo-mesh-pattern-rasterizer.c
        ../../src/cairo-mempool.c
        ../../src/cairo-misc.c
        ../../src/cairo-mono-scan-converter.c
        ../../src/cairo-mutex.c
        ../../src/cairo-no-compositor.c
        ../../src/cairo-observer.c
        ../../src/cairo-output-stream.c
        ../../src/cairo-paginated-surface.c
        ../../src/cairo-path-bounds.c
        ../../src/cairo-path.c
        ../../src/cairo-path-fill.c
        ../../src/cairo-path-fixed.c
        ../../src/cairo-path-in-fill.c
        ../../src/cairo-path-stroke.c
        ../../src/cairo-path-stroke-boxes.c
        ../../src/cairo-path-stroke-polygon.c
        ../../src/cairo-path-stroke-traps.c
        ../../src/cairo-path-stroke-tristrip.c
        ../../src/cairo-pattern.c
        ../../src/cairo-pen.c
        ../../src/cairo-polygon.c
        ../../src/cairo-polygon-intersect.c
        ../../src/cairo-polygon-reduce.c
        ../../src/cairo-raster-source-pattern.c
        ../../src/cairo-recording-surface.c
        ../../src/cairo-rectangle.c
        ../../src/cairo-rectangular-scan-converter.c
        ../../src/cairo-region.c
        ../../src/cairo-rtree.c
        ../../src/cairo-scaled-font.c
        ../../src/cairo-shape-mask-compositor.c
        ../../src/cairo-slope.c
        ../../src/cairo-spans.c
        ../../src/cairo-spans-compositor.c
        ../../src/cairo-spline.c
        ../../src/cairo-stroke-dash.c
        ../../src/cairo-stroke-style.c
        ../../src/cairo-surface.c
        ../../src/cairo-surface-clipper.c
        ../../src/cairo-surface-fallback.c
        ../../src/cairo-surface-observer.c
        ../../src/cairo-surface-offset.c
        ../../src/cairo-surface-snapshot.c
        ../../src/cairo-surface-subsurface.c
        ../../src/cairo-surface-wrapper.c
        ../../src/cairo-tag-attributes.c
        ../../src/cairo-tag-stack.c
        ../../src/cairo-time.c
        ../../src/cairo-tor-scan-converter.c
        ../../src/cairo-tor22-scan-converter.c
        ../../src/cairo-toy-font-face.c
        ../../src/cairo-traps.c
        ../../src/cairo-traps-compositor.c
        ../../src/cairo-tristrip.c
        ../../src/cairo-unicode.c
        ../../src/cairo-user-font.c
        ../../src/cairo-version.c
        ../../src/cairo-wideint.c
    )

include (${ANDROID_GRADLE_NATIVE_BUNDLE_PLUGIN_MK})
include_directories(../../COPKG/cairo)

add_library(
        # Specifies the name of the library.
        ${name}

        # Sets the library as a shared library.
        STATIC

        # Provides a relative path to your source file(s).
        ${srcs}
    )



add_definitions(-DCAIRO_NO_MUTEX=1 -DHAVE_STDINT_H -DHAVE_UINT64_T)

target_link_libraries(
        ${name}
        android log ${ANDROID_GRADLE_NATIVE_MODULES}
    )
