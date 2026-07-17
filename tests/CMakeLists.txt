include(FetchContent)
include(GoogleTest)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AuroraCopyRuntimeDLLs.cmake)

if (NOT TARGET gtest)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googletest)
endif ()

if (AURORA_ENABLE_GX)
  # GX FIFO round-trip tests
  # Compile GX sources directly to avoid pulling in the full renderer/WebGPU runtime
  add_executable(gx_fifo_tests
    gx_fifo_test.cpp
    gx_test_stubs.cpp
    # GX API implementations (encoders)
    ../lib/dolphin/gx/GXBump.cpp
    ../lib/dolphin/gx/GXCull.cpp
    ../lib/dolphin/gx/GXCpu2Efb.cpp
    ../lib/dolphin/gx/GXDispList.cpp
    ../lib/dolphin/gx/GXDraw.cpp
    ../lib/dolphin/gx/GXExtra.cpp
    ../lib/dolphin/gx/GXFifo.cpp
    ../lib/dolphin/gx/GXFrameBuffer.cpp
    ../lib/dolphin/gx/GXGeometry.cpp
    ../lib/dolphin/gx/GXGet.cpp
    ../lib/dolphin/gx/GXLighting.cpp
    ../lib/dolphin/gx/GXManage.cpp
    ../lib/dolphin/gx/GXAurora.cpp
    ../lib/dolphin/gx/GXPerf.cpp
    ../lib/dolphin/gx/GXPixel.cpp
    ../lib/dolphin/gx/GXTev.cpp
    ../lib/dolphin/gx/GXTexture.cpp
    ../lib/dolphin/gx/GXTransform.cpp
    ../lib/dolphin/gx/GXVert.cpp
    # FIFO/command processor (encoder + decoder)
    ../lib/gx/fifo.cpp
    ../lib/gx/command_processor.cpp
    # Display list reader/optimizer
    ../lib/gx/attr_fmt.cpp
    ../lib/gx/dl.cpp
    gx_dl_test.cpp
  )

  target_include_directories(gx_fifo_tests PRIVATE
    ../include
    ../lib
    ../lib/dolphin/gx
  )

  target_compile_definitions(gx_fifo_tests PRIVATE AURORA TARGET_PC)

  target_link_libraries(gx_fifo_tests PRIVATE
    gtest
    gtest_main
    fmt::fmt
    xxhash
    absl::flat_hash_map
    absl::btree
    dawn::dawncpp_headers
    TracyClient
    ${AURORA_SDL3_TARGET}
  )

  gtest_discover_tests(gx_fifo_tests)

  add_executable(render_worker_tests
    render_worker_test.cpp
    ../lib/gfx/render_worker.cpp
  )
  target_include_directories(render_worker_tests PRIVATE
    ../include
    ../lib
  )
  target_compile_definitions(render_worker_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(render_worker_tests PRIVATE
    gtest
    gtest_main
    TracyClient
  )
  gtest_discover_tests(render_worker_tests)

  add_executable(frame_capture_tests frame_capture_test.cpp)
  target_include_directories(frame_capture_tests PRIVATE ../include ../lib)
  target_compile_definitions(frame_capture_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(frame_capture_tests PRIVATE aurora::gx aurora::vi gtest gtest_main)
  aurora_copy_runtime_dlls(frame_capture_tests)
  gtest_discover_tests(frame_capture_tests)
endif () # AURORA_ENABLE_GX

# DVD API tests
if (TARGET aurora::dvd)
  add_executable(dvd_tests test_dvd.cpp)
  target_link_libraries(dvd_tests PRIVATE aurora::dvd gtest gtest_main)

  # Pass disc image path as a compile definition if provided
  if (DEFINED DVD_TEST_IMAGE AND EXISTS "${DVD_TEST_IMAGE}")
    target_compile_definitions(dvd_tests PRIVATE DVD_TEST_IMAGE="${DVD_TEST_IMAGE}")
  endif ()

  gtest_discover_tests(dvd_tests)
endif ()

# CARD API tests
if (TARGET aurora::card)
  # Compile the focused card implementation directly so the test does not pull
  # in aurora::core's renderer runtime through aurora::card.
  add_executable(card_tests
    card_gci_folder_test.cpp
    ../lib/card/BlockAllocationTable.cpp
    ../lib/card/CardGciFolder.cpp
    ../lib/card/File.cpp
    ../lib/card/FileIO.cpp
    ../lib/card/Util.cpp
  )
  target_include_directories(card_tests PRIVATE ../include ../lib)
  target_compile_definitions(card_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(card_tests PRIVATE
    gtest
    gtest_main
    fmt::fmt
    ${AURORA_SDL3_TARGET}
  )
  aurora_copy_runtime_dlls(card_tests)
  gtest_discover_tests(card_tests)
endif ()

add_executable(os_alloc_tests
  os_alloc_test.cpp
  os_test_globals.cpp
  ../lib/dolphin/os/OSAlloc.cpp
  ../lib/logging.cpp
)
target_include_directories(os_alloc_tests PRIVATE
  ../include
)
target_compile_definitions(os_alloc_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_alloc_tests PRIVATE gtest gtest_main fmt::fmt)
gtest_discover_tests(os_alloc_tests)

add_executable(os_time_tests
  os_time_test.cpp
  os_time_test_globals.cpp
  ../lib/dolphin/os/OSTime.cpp
  ../lib/logging.cpp
)
target_include_directories(os_time_tests PRIVATE ../include)
target_compile_definitions(os_time_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_time_tests PRIVATE gtest gtest_main fmt::fmt)
gtest_discover_tests(os_time_tests)
