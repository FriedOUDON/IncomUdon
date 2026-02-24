cmake_minimum_required(VERSION 3.16)

project(IncomUdon VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Network Multimedia QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(appIncomUdon
    main.cpp
)

qt_add_qml_module(appIncomUdon
    URI IncomUdon
    QML_FILES
        ui/Main.qml
    RESOURCES
        assets/sfx/ptt_on.wav
        assets/sfx/ptt_off.wav
        assets/sfx/carrier_sense.wav
        assets/sfx/README.md
    SOURCES
        audio/AudioInput.h
        audio/AudioInput.cpp
        audio/AudioOutput.h
        audio/AudioOutput.cpp
        audio/AudioBuffer.h
        audio/AudioBuffer.cpp
        audio/AudioResampler.h
        audio/AudioResampler.cpp
        codec/Codec2Wrapper.h
        codec/Codec2Wrapper.cpp
        core/AppState.h
        core/AppState.cpp
        core/AndroidPttBridge.h
        core/AndroidPttBridge.cpp
        core/ChannelManager.h
        core/ChannelManager.cpp
        core/PttController.h
        core/PttController.cpp
        crypto/AeadCipher.h
        crypto/AeadCipher.cpp
        crypto/KeyExchange.h
        crypto/KeyExchange.cpp
        net/packet.h
        net/packet.cpp
        net/Packetizer.h
        net/Packetizer.cpp
        net/Fec.h
        net/Fec.cpp
        net/JitterBuffer.h
        net/JitterBuffer.cpp
        net/udptransport.h
        net/udptransport.cpp
)

option(USE_CODEC2 "Build with bundled libcodec2" ON)
option(USE_CODEC2_ANDROID_PREBUILT "Use prebuilt libcodec2 for Android" ON)
option(USE_OPENSSL_ANDROID_PREBUILT "Use prebuilt OpenSSL for Android" ON)
option(USE_OPENSSL_WINDOWS_PREBUILT "Use prebuilt OpenSSL for Windows" ON)
option(USE_CODEC2_WINDOWS_PREBUILT "Use prebuilt libcodec2 for Windows" ON)
option(USE_CODEC2_SHARED_PREBUILT "Prefer prebuilt shared libcodec2 over static" ON)
option(USE_CODEC2_STATIC_PREBUILT_FALLBACK "Allow static libcodec2 fallback when shared is unavailable" ON)
option(USE_OPUS_ANDROID_PREBUILT "Use prebuilt libopus for Android" ON)
option(USE_OPUS_WINDOWS_PREBUILT "Use prebuilt libopus for Windows" ON)
option(USE_OPUS_SHARED_PREBUILT "Prefer prebuilt shared libopus over static" ON)
option(USE_OPUS_STATIC_PREBUILT_FALLBACK "Allow static libopus fallback when shared is unavailable" ON)
option(INCOMUDON_CODEC2_RUNTIME_LOADER "Enable runtime libcodec2 loading from app/UI" ON)

if(INCOMUDON_CODEC2_RUNTIME_LOADER)
    # Avoid symbol/ABI conflicts from mixing build-time linked codec2 and
    # runtime-loaded codec2 from user-selected files.
    set(USE_CODEC2 OFF CACHE BOOL "" FORCE)
    set(USE_CODEC2_ANDROID_PREBUILT OFF CACHE BOOL "" FORCE)
    set(USE_CODEC2_WINDOWS_PREBUILT OFF CACHE BOOL "" FORCE)
    set(USE_CODEC2_STATIC_PREBUILT_FALLBACK OFF CACHE BOOL "" FORCE)
    target_compile_definitions(appIncomUdon PRIVATE
        INCOMUDON_CODEC2_RUNTIME_LOADER=1
        INCOMUDON_USE_CODEC2=1)
endif()

set(INCOMUDON_CODEC2_INCLUDE "")
set(_codec2_include_candidates "")
if(ANDROID)
    list(APPEND _codec2_include_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_android/${ANDROID_ABI}/include")
elseif(WIN32)
    list(APPEND _codec2_include_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/include")
endif()
foreach(_codec2_include_candidate IN LISTS _codec2_include_candidates)
    if(INCOMUDON_CODEC2_INCLUDE)
        break()
    endif()
    if(NOT EXISTS "${_codec2_include_candidate}")
        continue()
    endif()
    if(EXISTS "${_codec2_include_candidate}/codec2.h" OR
       EXISTS "${_codec2_include_candidate}/codec2/codec2.h")
        set(INCOMUDON_CODEC2_INCLUDE "${_codec2_include_candidate}")
    endif()
endforeach()

if(INCOMUDON_CODEC2_INCLUDE)
    target_include_directories(appIncomUdon PRIVATE
        "${INCOMUDON_CODEC2_INCLUDE}"
        "${INCOMUDON_CODEC2_INCLUDE}/codec2")
    target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_CODEC2)
endif()

if(ANDROID)
    # libcodec2's build requires a native code generator when cross-compiling.
    # Use prebuilt libraries instead.
    set(USE_CODEC2 OFF CACHE BOOL "" FORCE)

    if(USE_CODEC2_ANDROID_PREBUILT)
        set(CODEC2_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_android")
        set(CODEC2_ANDROID_INCLUDE "${CODEC2_ANDROID_ROOT}/${ANDROID_ABI}/include")
        set(CODEC2_ANDROID_LIBDIR "${CODEC2_ANDROID_ROOT}/${ANDROID_ABI}/lib")
        set(CODEC2_ANDROID_LIB "")
        set(CODEC2_ANDROID_KIND "")

        if(USE_CODEC2_SHARED_PREBUILT)
            foreach(_cand IN ITEMS
                "${CODEC2_ANDROID_LIBDIR}/libcodec2.so")
                if(NOT CODEC2_ANDROID_LIB AND EXISTS "${_cand}")
                    set(CODEC2_ANDROID_LIB "${_cand}")
                    set(CODEC2_ANDROID_KIND "SHARED")
                endif()
            endforeach()
        endif()

        if(USE_CODEC2_STATIC_PREBUILT_FALLBACK AND NOT CODEC2_ANDROID_LIB)
            foreach(_cand IN ITEMS
                "${CODEC2_ANDROID_LIBDIR}/libcodec2.a")
                if(NOT CODEC2_ANDROID_LIB AND EXISTS "${_cand}")
                    set(CODEC2_ANDROID_LIB "${_cand}")
                    set(CODEC2_ANDROID_KIND "STATIC")
                endif()
            endforeach()
        endif()

        if(EXISTS "${CODEC2_ANDROID_LIB}")
            add_library(codec2 ${CODEC2_ANDROID_KIND} IMPORTED GLOBAL)
            set_target_properties(codec2 PROPERTIES
                IMPORTED_LOCATION "${CODEC2_ANDROID_LIB}"
            )
            target_link_libraries(appIncomUdon PRIVATE codec2)
            target_include_directories(appIncomUdon PRIVATE
                "${CODEC2_ANDROID_INCLUDE}"
                "${CODEC2_ANDROID_INCLUDE}/codec2"
            )
            target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_CODEC2)
            if(CODEC2_ANDROID_KIND STREQUAL "SHARED")
                get_target_property(_extra_libs appIncomUdon QT_ANDROID_EXTRA_LIBS)
                if(NOT _extra_libs)
                    set(_extra_libs "")
                endif()
                list(APPEND _extra_libs "${CODEC2_ANDROID_LIB}")
                set_target_properties(appIncomUdon PROPERTIES
                    QT_ANDROID_EXTRA_LIBS "${_extra_libs}")
            endif()
            message(STATUS "Using Android prebuilt libcodec2 (${CODEC2_ANDROID_KIND}): ${CODEC2_ANDROID_LIB}")
        else()
            message(WARNING "Android prebuilt libcodec2 not found under ${CODEC2_ANDROID_LIBDIR}")
        endif()
    endif()
else()
    if(WIN32 AND USE_CODEC2_WINDOWS_PREBUILT)
        set(CODEC2_WIN_ROOT_CANDIDATES "")
        if(DEFINED ENV{INCOMUDON_CODEC2_WINDOWS_ROOT})
            list(APPEND CODEC2_WIN_ROOT_CANDIDATES "$ENV{INCOMUDON_CODEC2_WINDOWS_ROOT}")
        endif()
        list(APPEND CODEC2_WIN_ROOT_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows")

        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND CODEC2_WIN_ROOT_CANDIDATES
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/x64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/win64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/amd64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/mingw64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/x64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/win64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/amd64"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/mingw64")
        else()
            list(APPEND CODEC2_WIN_ROOT_CANDIDATES
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/x86"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/win32"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2_windows/mingw32"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/x86"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/win32"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2-windows/mingw32")
        endif()

        foreach(_root IN LISTS CODEC2_WIN_ROOT_CANDIDATES)
            if(TARGET codec2)
                break()
            endif()
            if(NOT EXISTS "${_root}")
                continue()
            endif()

            set(_inc "")
            if(EXISTS "${_root}/include")
                set(_inc "${_root}/include")
            endif()

            set(_libdir "")
            foreach(_cand IN ITEMS
                "${_root}/lib"
                "${_root}/lib64"
                "${_root}/lib/x64"
                "${_root}/lib/Win32")
                if(NOT _libdir AND EXISTS "${_cand}")
                    set(_libdir "${_cand}")
                endif()
            endforeach()

            set(_lib "")
            set(_kind "")
            set(_dll "")
            set(_implib "")

            if(USE_CODEC2_SHARED_PREBUILT)
                foreach(_cand IN ITEMS
                    "${_root}/bin/libcodec2.dll"
                    "${_root}/bin/codec2.dll"
                    "${_libdir}/libcodec2.dll"
                    "${_libdir}/codec2.dll"
                    "${_root}/libcodec2.dll"
                    "${_root}/codec2.dll")
                    if(NOT _dll AND EXISTS "${_cand}")
                        set(_dll "${_cand}")
                    endif()
                endforeach()

                foreach(_cand IN ITEMS
                    "${_libdir}/libcodec2.dll.a"
                    "${_libdir}/codec2.dll.a"
                    "${_libdir}/libcodec2.lib"
                    "${_libdir}/codec2.lib")
                    if(NOT _implib AND EXISTS "${_cand}")
                        set(_implib "${_cand}")
                    endif()
                endforeach()

                if(_dll AND _implib)
                    set(_lib "${_implib}")
                    set(_kind "SHARED")
                endif()
            endif()

            if(USE_CODEC2_STATIC_PREBUILT_FALLBACK AND NOT _lib)
                foreach(_cand IN ITEMS
                    "${_libdir}/libcodec2.a"
                    "${_libdir}/libcodec2.lib"
                    "${_libdir}/codec2.lib")
                    if(NOT _lib AND EXISTS "${_cand}")
                        set(_lib "${_cand}")
                        set(_kind "STATIC")
                    endif()
                endforeach()
            endif()

            if(_inc AND _lib)
                add_library(codec2 ${_kind} IMPORTED GLOBAL)
                if(_kind STREQUAL "SHARED")
                    set_target_properties(codec2 PROPERTIES
                        IMPORTED_IMPLIB "${_lib}"
                        IMPORTED_LOCATION "${_dll}")
                else()
                    set_target_properties(codec2 PROPERTIES
                        IMPORTED_LOCATION "${_lib}")
                endif()
                target_link_libraries(appIncomUdon PRIVATE codec2)
                target_include_directories(appIncomUdon PRIVATE
                    "${_inc}"
                    "${_inc}/codec2")
                target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_CODEC2)
                if(_kind STREQUAL "SHARED" AND _dll)
                    add_custom_command(TARGET appIncomUdon POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll}"
                        $<TARGET_FILE_DIR:appIncomUdon>)
                endif()
                message(STATUS "Using Windows prebuilt libcodec2 (${_kind}): ${_lib}")
            endif()
        endforeach()
    endif()

    if(USE_CODEC2 AND NOT TARGET codec2)
        set(CODEC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libcodec2")
        if(EXISTS "${CODEC2_ROOT}/CMakeLists.txt")
            set(UNITTEST OFF CACHE BOOL "" FORCE)
            set(INSTALL_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(LPCNET OFF CACHE BOOL "" FORCE)
            set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

            set(CODEC2_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/libcodec2-build")
            add_subdirectory("${CODEC2_ROOT}" "${CODEC2_BUILD_DIR}" EXCLUDE_FROM_ALL)
            if(TARGET codec2)
                target_compile_definitions(codec2 PRIVATE
                    CODEC2_MODE_EN_DEFAULT=1
                    CODEC2_MODE_700_EN=1
                    CODEC2_MODE_700C_EN=1
                    CODEC2_MODE_450_EN=1
                )
            endif()
            target_link_libraries(appIncomUdon PRIVATE codec2)
            target_include_directories(appIncomUdon PRIVATE "${CODEC2_BUILD_DIR}")
            target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_CODEC2)
        else()
            message(WARNING "libcodec2 not found at ${CODEC2_ROOT}")
        endif()
    elseif(USE_CODEC2 AND NOT TARGET codec2)
        message(WARNING "libcodec2 target not found. Falling back to no codec2.")
    endif()
endif()

# Opus (Android/Windows prebuilt)
set(INCOMUDON_OPUS_FOUND FALSE)

if(ANDROID AND USE_OPUS_ANDROID_PREBUILT)
    set(OPUS_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_android")
    set(OPUS_ANDROID_INCLUDE "${OPUS_ANDROID_ROOT}/${ANDROID_ABI}/include")
    set(OPUS_ANDROID_LIBDIR "${OPUS_ANDROID_ROOT}/${ANDROID_ABI}/lib")
    set(OPUS_ANDROID_LIB "")
    set(OPUS_ANDROID_KIND "")

    if(EXISTS "${OPUS_ANDROID_INCLUDE}" AND EXISTS "${OPUS_ANDROID_LIBDIR}")
        if(USE_OPUS_SHARED_PREBUILT)
            foreach(_cand IN ITEMS
                "${OPUS_ANDROID_LIBDIR}/libopus.so"
                "${OPUS_ANDROID_LIBDIR}/opus.so")
                if(NOT OPUS_ANDROID_LIB AND EXISTS "${_cand}")
                    set(OPUS_ANDROID_LIB "${_cand}")
                    set(OPUS_ANDROID_KIND "SHARED")
                endif()
            endforeach()
        endif()

        if(USE_OPUS_STATIC_PREBUILT_FALLBACK AND NOT OPUS_ANDROID_LIB)
            foreach(_cand IN ITEMS
                "${OPUS_ANDROID_LIBDIR}/libopus.a"
                "${OPUS_ANDROID_LIBDIR}/opus.a")
                if(NOT OPUS_ANDROID_LIB AND EXISTS "${_cand}")
                    set(OPUS_ANDROID_LIB "${_cand}")
                    set(OPUS_ANDROID_KIND "STATIC")
                endif()
            endforeach()
        endif()

        if(OPUS_ANDROID_LIB)
            add_library(incomudon_opus ${OPUS_ANDROID_KIND} IMPORTED GLOBAL)
            set_target_properties(incomudon_opus PROPERTIES
                IMPORTED_LOCATION "${OPUS_ANDROID_LIB}")
            target_link_libraries(appIncomUdon PRIVATE incomudon_opus)
            target_include_directories(appIncomUdon PRIVATE
                "${OPUS_ANDROID_INCLUDE}"
                "${OPUS_ANDROID_INCLUDE}/opus")
            target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_OPUS)
            if(OPUS_ANDROID_KIND STREQUAL "SHARED")
                get_target_property(_extra_libs appIncomUdon QT_ANDROID_EXTRA_LIBS)
                if(NOT _extra_libs)
                    set(_extra_libs "")
                endif()
                list(APPEND _extra_libs "${OPUS_ANDROID_LIB}")
                set_target_properties(appIncomUdon PROPERTIES
                    QT_ANDROID_EXTRA_LIBS "${_extra_libs}")
            endif()
            message(STATUS "Using Android prebuilt libopus (${OPUS_ANDROID_KIND}): ${OPUS_ANDROID_LIB}")
            set(INCOMUDON_OPUS_FOUND TRUE)
        else()
            message(WARNING "Android prebuilt libopus not found under ${OPUS_ANDROID_LIBDIR}")
        endif()
    endif()
endif()

if(WIN32 AND USE_OPUS_WINDOWS_PREBUILT AND NOT INCOMUDON_OPUS_FOUND)
    set(OPUS_WIN_ROOT_CANDIDATES "")
    if(DEFINED ENV{INCOMUDON_OPUS_WINDOWS_ROOT})
        list(APPEND OPUS_WIN_ROOT_CANDIDATES "$ENV{INCOMUDON_OPUS_WINDOWS_ROOT}")
    endif()
    list(APPEND OPUS_WIN_ROOT_CANDIDATES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND OPUS_WIN_ROOT_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/x64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/win64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/amd64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/mingw64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/x64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/win64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/amd64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/mingw64")
    else()
        list(APPEND OPUS_WIN_ROOT_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/x86"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/win32"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus_windows/mingw32"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/x86"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/win32"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopus-windows/mingw32")
    endif()

    foreach(_root IN LISTS OPUS_WIN_ROOT_CANDIDATES)
        if(TARGET incomudon_opus)
            break()
        endif()
        if(NOT EXISTS "${_root}")
            continue()
        endif()

        set(_inc "")
        if(EXISTS "${_root}/include")
            set(_inc "${_root}/include")
        endif()

        set(_libdir "")
        foreach(_cand IN ITEMS
            "${_root}/lib"
            "${_root}/lib64"
            "${_root}/lib/x64"
            "${_root}/lib/Win32")
            if(NOT _libdir AND EXISTS "${_cand}")
                set(_libdir "${_cand}")
            endif()
        endforeach()

        set(_lib "")
        set(_kind "")
        set(_dll "")
        set(_implib "")

        if(USE_OPUS_SHARED_PREBUILT)
            foreach(_cand IN ITEMS
                "${_root}/bin/libopus.dll"
                "${_root}/bin/opus.dll"
                "${_libdir}/libopus.dll"
                "${_libdir}/opus.dll"
                "${_root}/libopus.dll"
                "${_root}/opus.dll")
                if(NOT _dll AND EXISTS "${_cand}")
                    set(_dll "${_cand}")
                endif()
            endforeach()

            foreach(_cand IN ITEMS
                "${_libdir}/libopus.dll.a"
                "${_libdir}/opus.dll.a"
                "${_libdir}/libopus.lib"
                "${_libdir}/opus.lib")
                if(NOT _implib AND EXISTS "${_cand}")
                    set(_implib "${_cand}")
                endif()
            endforeach()

            if(_dll AND _implib)
                set(_lib "${_implib}")
                set(_kind "SHARED")
            endif()
        endif()

        if(USE_OPUS_STATIC_PREBUILT_FALLBACK AND NOT _lib)
            foreach(_cand IN ITEMS
                "${_libdir}/libopus.a"
                "${_libdir}/libopus.lib"
                "${_libdir}/opus.lib")
                if(NOT _lib AND EXISTS "${_cand}")
                    set(_lib "${_cand}")
                    set(_kind "STATIC")
                endif()
            endforeach()
        endif()

        if(_inc AND _lib)
            add_library(incomudon_opus ${_kind} IMPORTED GLOBAL)
            if(_kind STREQUAL "SHARED")
                set_target_properties(incomudon_opus PROPERTIES
                    IMPORTED_IMPLIB "${_lib}"
                    IMPORTED_LOCATION "${_dll}")
            else()
                set_target_properties(incomudon_opus PROPERTIES
                    IMPORTED_LOCATION "${_lib}")
            endif()
            target_link_libraries(appIncomUdon PRIVATE incomudon_opus)
            target_include_directories(appIncomUdon PRIVATE
                "${_inc}"
                "${_inc}/opus")
            target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_OPUS)
            if(_kind STREQUAL "SHARED" AND _dll)
                add_custom_command(TARGET appIncomUdon POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}"
                    $<TARGET_FILE_DIR:appIncomUdon>)
            endif()
            message(STATUS "Using Windows prebuilt libopus (${_kind}): ${_lib}")
            set(INCOMUDON_OPUS_FOUND TRUE)
        endif()
    endforeach()
endif()

# OpenSSL (Android/Windows prebuilt or host)
set(INCOMUDON_OPENSSL_FOUND FALSE)
if(ANDROID AND USE_OPENSSL_ANDROID_PREBUILT)
    set(_openssl_android_roots
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_android/${ANDROID_ABI}")
    # Backward compatibility for misnamed arm64 directory.
    if(ANDROID_ABI STREQUAL "arm64-v8a")
        list(APPEND _openssl_android_roots
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_android/armeabi-v8a")
    elseif(ANDROID_ABI STREQUAL "armeabi-v8a")
        list(APPEND _openssl_android_roots
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_android/arm64-v8a")
    endif()

    foreach(OPENSSL_ANDROID_ROOT IN LISTS _openssl_android_roots)
        if(INCOMUDON_OPENSSL_FOUND)
            break()
        endif()

        set(OPENSSL_ANDROID_INCLUDE "${OPENSSL_ANDROID_ROOT}/include")
        set(OPENSSL_ANDROID_LIBDIR "${OPENSSL_ANDROID_ROOT}/lib")
        if(NOT (EXISTS "${OPENSSL_ANDROID_INCLUDE}" AND EXISTS "${OPENSSL_ANDROID_LIBDIR}"))
            continue()
        endif()

        set(OPENSSL_SSL_CANDIDATES
            "${OPENSSL_ANDROID_LIBDIR}/libssl.so"
            "${OPENSSL_ANDROID_LIBDIR}/libssl_3.so"
            "${OPENSSL_ANDROID_LIBDIR}/libssl_1_1.so"
            "${OPENSSL_ANDROID_LIBDIR}/libssl.a")
        set(OPENSSL_CRYPTO_CANDIDATES
            "${OPENSSL_ANDROID_LIBDIR}/libcrypto.so"
            "${OPENSSL_ANDROID_LIBDIR}/libcrypto_3.so"
            "${OPENSSL_ANDROID_LIBDIR}/libcrypto_1_1.so"
            "${OPENSSL_ANDROID_LIBDIR}/libcrypto.a")

        set(OPENSSL_SSL_FOUND "")
        foreach(_cand IN LISTS OPENSSL_SSL_CANDIDATES)
            if(EXISTS "${_cand}")
                list(APPEND OPENSSL_SSL_FOUND "${_cand}")
            endif()
        endforeach()

        set(OPENSSL_CRYPTO_FOUND "")
        foreach(_cand IN LISTS OPENSSL_CRYPTO_CANDIDATES)
            if(EXISTS "${_cand}")
                list(APPEND OPENSSL_CRYPTO_FOUND "${_cand}")
            endif()
        endforeach()

        if(NOT (OPENSSL_SSL_FOUND AND OPENSSL_CRYPTO_FOUND))
            continue()
        endif()

        list(GET OPENSSL_SSL_FOUND 0 OPENSSL_ANDROID_SSL)
        list(GET OPENSSL_CRYPTO_FOUND 0 OPENSSL_ANDROID_CRYPTO)

        if(NOT TARGET OpenSSL::SSL)
            add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
        endif()
        if(NOT TARGET OpenSSL::Crypto)
            add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
        endif()

        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_ANDROID_SSL}")
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_ANDROID_CRYPTO}")

        target_include_directories(appIncomUdon PRIVATE "${OPENSSL_ANDROID_INCLUDE}")

        # If using shared libs, bundle them into the APK.
        get_filename_component(_ssl_ext "${OPENSSL_ANDROID_SSL}" EXT)
        if(_ssl_ext STREQUAL ".so")
            get_target_property(_extra_libs appIncomUdon QT_ANDROID_EXTRA_LIBS)
            if(NOT _extra_libs)
                set(_extra_libs "")
            endif()
            list(APPEND _extra_libs "${OPENSSL_ANDROID_SSL}" "${OPENSSL_ANDROID_CRYPTO}")
            set_target_properties(appIncomUdon PROPERTIES
                QT_ANDROID_EXTRA_LIBS "${_extra_libs}")
        endif()

        message(STATUS "Using Android OpenSSL prebuilt from: ${OPENSSL_ANDROID_ROOT}")
        set(INCOMUDON_OPENSSL_FOUND TRUE)
    endforeach()
endif()

if(WIN32 AND USE_OPENSSL_WINDOWS_PREBUILT AND NOT INCOMUDON_OPENSSL_FOUND)
    if(MINGW)
        # MinGW toolchains cannot link MSVC-style OpenSSL .lib static libraries.
        # Prefer GNU-style archives/import libraries.
        set(_openssl_win_ssl_name_candidates
            "libssl.a"
            "libssl-3.dll.a"
            "libssl-3-x64.dll.a"
            "libssl-1_1.dll.a"
            "libssl.dll.a")
        set(_openssl_win_crypto_name_candidates
            "libcrypto.a"
            "libcrypto-3.dll.a"
            "libcrypto-3-x64.dll.a"
            "libcrypto-1_1.dll.a"
            "libcrypto.dll.a")
    else()
        set(_openssl_win_ssl_name_candidates
            "libssl.lib"
            "libssl-3.lib"
            "libssl-3-x64.lib"
            "libssl-1_1.lib"
            "ssl.lib"
            "libssl.a"
            "libssl-3.dll.a"
            "libssl-3-x64.dll.a"
            "libssl-1_1.dll.a"
            "libssl.dll.a")
        set(_openssl_win_crypto_name_candidates
            "libcrypto.lib"
            "libcrypto-3.lib"
            "libcrypto-3-x64.lib"
            "libcrypto-1_1.lib"
            "crypto.lib"
            "libcrypto.a"
            "libcrypto-3.dll.a"
            "libcrypto-3-x64.dll.a"
            "libcrypto-1_1.dll.a"
            "libcrypto.dll.a")
    endif()
    set(_openssl_win_ssl_msvc_name_candidates
        "libssl.lib"
        "libssl-3.lib"
        "libssl-3-x64.lib"
        "libssl-1_1.lib"
        "ssl.lib")
    set(_openssl_win_crypto_msvc_name_candidates
        "libcrypto.lib"
        "libcrypto-3.lib"
        "libcrypto-3-x64.lib"
        "libcrypto-1_1.lib"
        "crypto.lib")
    set(_openssl_win_msvc_warning_emitted FALSE)

    set(_openssl_win_include_hint "")
    set(_openssl_win_lib_hint "")
    if(DEFINED ENV{INCOMUDON_OPENSSL_WINDOWS_INCLUDE})
        set(_openssl_win_include_hint "$ENV{INCOMUDON_OPENSSL_WINDOWS_INCLUDE}")
    endif()
    if(DEFINED ENV{INCOMUDON_OPENSSL_WINDOWS_LIBDIR})
        set(_openssl_win_lib_hint "$ENV{INCOMUDON_OPENSSL_WINDOWS_LIBDIR}")
    endif()

    if(_openssl_win_include_hint AND _openssl_win_lib_hint)
        if(EXISTS "${_openssl_win_include_hint}" AND EXISTS "${_openssl_win_lib_hint}")
            set(_ssl "")
            foreach(_name IN LISTS _openssl_win_ssl_name_candidates)
                set(_cand "${_openssl_win_lib_hint}/${_name}")
                if(NOT _ssl AND EXISTS "${_cand}")
                    set(_ssl "${_cand}")
                endif()
            endforeach()

            set(_crypto "")
            foreach(_name IN LISTS _openssl_win_crypto_name_candidates)
                set(_cand "${_openssl_win_lib_hint}/${_name}")
                if(NOT _crypto AND EXISTS "${_cand}")
                    set(_crypto "${_cand}")
                endif()
            endforeach()

            if(MINGW AND NOT (_ssl AND _crypto) AND NOT _openssl_win_msvc_warning_emitted)
                set(_hint_has_msvc_ssl FALSE)
                foreach(_name IN LISTS _openssl_win_ssl_msvc_name_candidates)
                    if(EXISTS "${_openssl_win_lib_hint}/${_name}")
                        set(_hint_has_msvc_ssl TRUE)
                    endif()
                endforeach()
                set(_hint_has_msvc_crypto FALSE)
                foreach(_name IN LISTS _openssl_win_crypto_msvc_name_candidates)
                    if(EXISTS "${_openssl_win_lib_hint}/${_name}")
                        set(_hint_has_msvc_crypto TRUE)
                    endif()
                endforeach()
                if(_hint_has_msvc_ssl AND _hint_has_msvc_crypto)
                    message(WARNING
                        "Found MSVC-style OpenSSL .lib files under ${_openssl_win_lib_hint}, "
                        "but current compiler is MinGW. These are incompatible and ignored. "
                        "Use MinGW-built libssl.a/libcrypto.a (or *.dll.a), or build with an MSVC kit.")
                    set(_openssl_win_msvc_warning_emitted TRUE)
                endif()
            endif()

            if(_ssl AND _crypto)
                if(NOT TARGET OpenSSL::SSL)
                    add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
                endif()
                if(NOT TARGET OpenSSL::Crypto)
                    add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
                endif()

                set_target_properties(OpenSSL::SSL PROPERTIES
                    IMPORTED_LOCATION "${_ssl}")
                set_target_properties(OpenSSL::Crypto PROPERTIES
                    IMPORTED_LOCATION "${_crypto}")

                target_include_directories(appIncomUdon PRIVATE "${_openssl_win_include_hint}")

                file(GLOB _ssl_dlls
                    "${_openssl_win_lib_hint}/*ssl*.dll"
                    "${_openssl_win_lib_hint}/../bin/*ssl*.dll")
                file(GLOB _crypto_dlls
                    "${_openssl_win_lib_hint}/*crypto*.dll"
                    "${_openssl_win_lib_hint}/../bin/*crypto*.dll")
                set(_openssl_win_dlls ${_ssl_dlls} ${_crypto_dlls})
                if(_openssl_win_dlls)
                    add_custom_command(TARGET appIncomUdon POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        ${_openssl_win_dlls}
                        $<TARGET_FILE_DIR:appIncomUdon>)
                endif()

                set(INCOMUDON_OPENSSL_FOUND TRUE)
            endif()
        endif()
    endif()

    set(OPENSSL_WIN_ROOT_CANDIDATES "")
    if(DEFINED ENV{INCOMUDON_OPENSSL_WINDOWS_ROOT})
        list(APPEND OPENSSL_WIN_ROOT_CANDIDATES "$ENV{INCOMUDON_OPENSSL_WINDOWS_ROOT}")
    endif()
    if(DEFINED OPENSSL_ROOT_DIR)
        list(APPEND OPENSSL_WIN_ROOT_CANDIDATES "${OPENSSL_ROOT_DIR}")
    endif()
    if(DEFINED ENV{OPENSSL_ROOT_DIR})
        list(APPEND OPENSSL_WIN_ROOT_CANDIDATES "$ENV{OPENSSL_ROOT_DIR}")
    endif()
    list(APPEND OPENSSL_WIN_ROOT_CANDIDATES
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        list(APPEND OPENSSL_WIN_ROOT_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/x64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/win64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/amd64"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/mingw64")
    else()
        list(APPEND OPENSSL_WIN_ROOT_CANDIDATES
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/x86"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/win32"
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openssl_windows/mingw32")
    endif()

    foreach(_root IN LISTS OPENSSL_WIN_ROOT_CANDIDATES)
        if(INCOMUDON_OPENSSL_FOUND)
            continue()
        endif()
        if(NOT EXISTS "${_root}")
            continue()
        endif()

        set(_inc "${_root}/include")
        if(NOT EXISTS "${_inc}")
            continue()
        endif()

        set(_libdir "")
        foreach(_cand IN ITEMS
            "${_root}/lib"
            "${_root}/lib64"
            "${_root}/lib/x64"
            "${_root}/lib/Win32")
            if(NOT _libdir AND EXISTS "${_cand}")
                set(_libdir "${_cand}")
            endif()
        endforeach()
        if(NOT _libdir)
            continue()
        endif()

        set(_ssl "")
        foreach(_name IN LISTS _openssl_win_ssl_name_candidates)
            set(_cand "${_libdir}/${_name}")
            if(NOT _ssl AND EXISTS "${_cand}")
                set(_ssl "${_cand}")
            endif()
        endforeach()

        set(_crypto "")
        foreach(_name IN LISTS _openssl_win_crypto_name_candidates)
            set(_cand "${_libdir}/${_name}")
            if(NOT _crypto AND EXISTS "${_cand}")
                set(_crypto "${_cand}")
            endif()
        endforeach()

        if(MINGW AND NOT (_ssl AND _crypto) AND NOT _openssl_win_msvc_warning_emitted)
            set(_root_has_msvc_ssl FALSE)
            foreach(_name IN LISTS _openssl_win_ssl_msvc_name_candidates)
                if(EXISTS "${_libdir}/${_name}")
                    set(_root_has_msvc_ssl TRUE)
                endif()
            endforeach()
            set(_root_has_msvc_crypto FALSE)
            foreach(_name IN LISTS _openssl_win_crypto_msvc_name_candidates)
                if(EXISTS "${_libdir}/${_name}")
                    set(_root_has_msvc_crypto TRUE)
                endif()
            endforeach()
            if(_root_has_msvc_ssl AND _root_has_msvc_crypto)
                message(WARNING
                    "Found MSVC-style OpenSSL .lib files under ${_libdir}, "
                    "but current compiler is MinGW. These are incompatible and ignored. "
                    "Use MinGW-built libssl.a/libcrypto.a (or *.dll.a), or build with an MSVC kit.")
                set(_openssl_win_msvc_warning_emitted TRUE)
            endif()
        endif()

        if(_ssl AND _crypto)
            if(NOT TARGET OpenSSL::SSL)
                add_library(OpenSSL::SSL UNKNOWN IMPORTED GLOBAL)
            endif()
            if(NOT TARGET OpenSSL::Crypto)
                add_library(OpenSSL::Crypto UNKNOWN IMPORTED GLOBAL)
            endif()

            set_target_properties(OpenSSL::SSL PROPERTIES
                IMPORTED_LOCATION "${_ssl}")
            set_target_properties(OpenSSL::Crypto PROPERTIES
                IMPORTED_LOCATION "${_crypto}")

            target_include_directories(appIncomUdon PRIVATE "${_inc}")

            file(GLOB _ssl_dlls
                "${_root}/bin/*ssl*.dll"
                "${_libdir}/*ssl*.dll")
            file(GLOB _crypto_dlls
                "${_root}/bin/*crypto*.dll"
                "${_libdir}/*crypto*.dll")
            set(_openssl_win_dlls ${_ssl_dlls} ${_crypto_dlls})
            if(_openssl_win_dlls)
                add_custom_command(TARGET appIncomUdon POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_openssl_win_dlls}
                    $<TARGET_FILE_DIR:appIncomUdon>)
            endif()

            set(INCOMUDON_OPENSSL_FOUND TRUE)
        endif()
    endforeach()
endif()

if(NOT INCOMUDON_OPENSSL_FOUND)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(INCOMUDON_OPENSSL_FOUND TRUE)
    endif()
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
set_target_properties(appIncomUdon PROPERTIES
#    MACOSX_BUNDLE_GUI_IDENTIFIER com.example.appIncomUdon
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(ANDROID)
    set_property(TARGET appIncomUdon PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/android")
    set_target_properties(appIncomUdon PROPERTIES
        OUTPUT_NAME "IncomUdon"
    )
    if(DEFINED ENV{INCOMUDON_ANDROID_BUILD_DIR})
        file(TO_CMAKE_PATH "$ENV{INCOMUDON_ANDROID_BUILD_DIR}" _android_build_dir)
        set_property(TARGET appIncomUdon PROPERTY QT_ANDROID_BUILD_DIR
            "${_android_build_dir}")
    endif()
endif()

if(IOS)
    set_target_properties(appIncomUdon PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist"
    )
endif()

target_include_directories(appIncomUdon
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(appIncomUdon
    PRIVATE Qt6::Quick
    PRIVATE Qt6::QuickControls2
    PRIVATE Qt6::Network
    PRIVATE Qt6::Multimedia
)

if(INCOMUDON_OPENSSL_FOUND)
    target_link_libraries(appIncomUdon PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(appIncomUdon PRIVATE INCOMUDON_USE_OPENSSL)
    if(WIN32)
        # OpenSSL on Windows depends on system crypto/SSL libraries.
        target_link_libraries(appIncomUdon PRIVATE
            crypt32
            ws2_32
            advapi32
            user32
            gdi32)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS appIncomUdon
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
