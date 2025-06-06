CMAKE_MINIMUM_REQUIRED(VERSION 3.25.1)
SET(CMAKE_CXX_STANDARD 23)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -DNDEBUG")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -DNDEBUG")
PROJECT(avitab-browser C CXX)

IF(APPLE)
    SET(DIRECTORY_PREFIX "mac_x64")
    SET(XPLANE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/Libraries/Mac/")
    ELSEIF(UNIX)
    SET(DIRECTORY_PREFIX "lin_x64")
    SET(XPLANE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/Libraries/Lin/")
ELSEIF(WIN32)
    SET(DIRECTORY_PREFIX "win_x64")
    SET(XPLANE_LIBRARY_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/Libraries/Win/")
ENDIF()

SET(XPLANE_INCLUDES_PATH "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/" CACHE STRING PATH)

IF(APPLE)
    FIND_PACKAGE(CURL REQUIRED)
    SET(CURL_LIBRARY CURL::libcurl)
    IF (NOT APPLE OR SDK_VERSION GREATER 400) # Don't include CEF for X-Plane 11 on macOS - cefclient Helper.app handles this.
        FIND_LIBRARY(CEF_LIBRARY "Chromium Embedded Framework" PATHS "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef")
    ENDIF()
    SET(CEF_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef/libcef_dll_wrapper.a")
    FIND_LIBRARY(COCOA_LIBRARY Cocoa)
    FIND_LIBRARY(OPENGL_LIBRARY OpenGL)
	FIND_LIBRARY(XPLM_LIBRARY XPLM "${XPLANE_LIBRARY_PATH}")
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets "${XPLANE_LIBRARY_PATH}")
	ADD_LIBRARY(xplm STATIC IMPORTED GLOBAL)
	ADD_LIBRARY(xpwidgets STATIC IMPORTED GLOBAL)
	SET_PROPERTY(TARGET xplm PROPERTY IMPORTED_LOCATION "${XPLM_LIBRARY}")
	SET_PROPERTY(TARGET xpwidgets PROPERTY IMPORTED_LOCATION "${XPWIDGETS_LIBRARY}")
ELSEIF(WIN32)
    FIND_LIBRARY(CEF_LIBRARY NAMES libcef PATHS "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef/Release")
    SET(CEF_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef/libcef_dll_wrapper/libcef_dll_wrapper.a")
    FIND_LIBRARY(OPENGL_LIBRARY NAMES opengl32 PATHS "/opt/homebrew/Cellar/mingw-w64/*/toolchain-x86_64/x86_64-w64-mingw32/lib")
    FIND_LIBRARY(CURL_LIBRARY NAMES libcurl PATHS "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/curl/lib")
	FIND_LIBRARY(XPLM_LIBRARY XPLM_64 "${XPLANE_LIBRARY_PATH}")
	FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets_64 "${XPLANE_LIBRARY_PATH}")
	ADD_LIBRARY(xplm SHARED IMPORTED GLOBAL)
	ADD_LIBRARY(xpwidgets SHARED IMPORTED GLOBAL)
	SET_PROPERTY(TARGET xplm PROPERTY IMPORTED_IMPLIB "${XPLM_LIBRARY}")
	SET_PROPERTY(TARGET xpwidgets PROPERTY IMPORTED_IMPLIB "${XPWIDGETS_LIBRARY}")
ELSEIF(UNIX)
    FIND_PACKAGE(CURL REQUIRED)
    FIND_LIBRARY(OPENSSL_LIBRARY NAMES libssl.so PATHS "/usr/lib/x86_64-linux-gnu")
    FIND_LIBRARY(OPENSSL_CRYPTO_LIBRARY NAMES libcrypto.so PATHS "/usr/lib/x86_64-linux-gnu")
    FIND_LIBRARY(CURL_LIBRARY NAMES libcurl.so PATHS "/usr/lib/x86_64-linux-gnu")
    FIND_LIBRARY(CEF_LIBRARY libcef.so PATHS "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef/Release" NO_DEFAULT_PATH)
    SET(CEF_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef/libcef_dll_wrapper/libcef_dll_wrapper.a")
    FIND_PACKAGE(OpenGL REQUIRED)
    FIND_LIBRARY(XPLM_LIBRARY XPLM_64.so PATHS "${XPLANE_LIBRARY_PATH}" REQUIRED)
    FIND_LIBRARY(XPWIDGETS_LIBRARY XPWidgets_64.so PATHS "${XPLANE_LIBRARY_PATH}" REQUIRED)
    ADD_LIBRARY(xplm SHARED IMPORTED GLOBAL)
    ADD_LIBRARY(xpwidgets SHARED IMPORTED GLOBAL)
    SET_PROPERTY(TARGET xplm PROPERTY IMPORTED_LOCATION "${XPLM_LIBRARY}")
    SET_PROPERTY(TARGET xpwidgets PROPERTY IMPORTED_LOCATION "${XPWIDGETS_LIBRARY}")

ENDIF()

SET_PROPERTY(TARGET xplm APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/XPLM" "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/Wrappers")
SET_PROPERTY(TARGET xpwidgets APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/XPLM" "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/Widgets" "${CMAKE_CURRENT_SOURCE_DIR}/SDK_${SDK_VERSION}/CHeaders/Wrappers")

FUNCTION(HEADER_DIRECTORIES return_list base_dir)
    FILE(GLOB_RECURSE new_list "${base_dir}/*.h" "${base_dir}/*.hpp")
    SET(dir_list "")
    FOREACH(file_path ${new_list})
        GET_FILENAME_COMPONENT(dir_path ${file_path} PATH)
        LIST(APPEND dir_list ${dir_path})
    ENDFOREACH()
    LIST(REMOVE_DUPLICATES dir_list)
    SET(${return_list} ${dir_list} PARENT_SCOPE)
ENDFUNCTION()

FUNCTION(FIND_SOURCE_FILES return_list base_dir)
    SET(file_patterns "${base_dir}/*.c" "${base_dir}/*.cpp" "${base_dir}/*.hpp")

    IF(APPLE)
        LIST(APPEND file_patterns "${base_dir}/*.mm")
    ENDIF()
    FILE(GLOB_RECURSE new_list ${file_patterns})

    LIST(FIND new_list "${base_dir}/main.cpp" main_index)
    IF(main_index GREATER_EQUAL 0)
        LIST(REMOVE_AT new_list ${main_index})
        LIST(PREPEND new_list "${base_dir}/main.cpp")
    ENDIF()

    SET(${return_list} ${new_list} PARENT_SCOPE)
ENDFUNCTION()

FUNCTION(add_xplane_sdk_definitions library_name library_version) 
    IF(APPLE)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DAPL=1 -DIBM=0 -DLIN=0)
    ELSEIF(UNIX)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DAPL=0 -DIBM=0 -DLIN=1)
        TARGET_COMPILE_OPTIONS(${library_name} PRIVATE "-static")
        TARGET_LINK_OPTIONS(${library_name} PRIVATE "-static-libstdc++" "-static-libgcc")
    ELSEIF(WIN32)
        TARGET_COMPILE_OPTIONS(${library_name} PRIVATE "-static")
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DAPL=0 -DIBM=1 -DLIN=0 -DUSING_CEF_SHARED=1)
        TARGET_LINK_OPTIONS(${library_name} PRIVATE "-static-libstdc++" "-static-libgcc" "-static" "-Wl,--allow-multiple-definition")
    ENDIF()

    IF(library_version EQUAL 200)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1)
    ELSEIF(library_version EQUAL 210)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1 -DXPLM210=1)
    ELSEIF(library_version EQUAL 300)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1 -DXPLM210=1 -DXPLM300=1)
    ELSEIF(library_version EQUAL 301)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1)
    ELSEIF(library_version EQUAL 400)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM400=1)
    ELSEIF(library_version EQUAL 410)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM400=1 -DXPLM410=1)
    ELSE()
        MESSAGE(FATAL_ERROR "Library version one of: 200, 210, 300, 301, 400, 410")
    ENDIF()
    TARGET_INCLUDE_DIRECTORIES(${library_name} PUBLIC "${XPLANE_INCLUDES_PATH}/XPLM" "${XPLANE_INCLUDES_PATH}/Widgets" "${XPLANE_INCLUDES_PATH}/Wrappers")
ENDFUNCTION(add_xplane_sdk_definitions)

FUNCTION(add_xplane_plugin library_name library_version src_dir)
    MESSAGE(STATUS "Preparing X-Plane plugin '${library_name}' at ${src_dir}")
    FIND_SOURCE_FILES(FILES ${src_dir})

    IF(APPLE)
        ADD_EXECUTABLE(${library_name} ${FILES})
    ELSEIF(UNIX)
        ADD_LIBRARY(${library_name} SHARED ${FILES})
    ELSEIF(WIN32)
        ADD_LIBRARY(${library_name} MODULE ${FILES})
        # TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC -D_CRT_SECURE_NO_WARNINGS /bigobj)
        TARGET_COMPILE_DEFINITIONS(${library_name} PUBLIC _CRT_SECURE_NO_WARNINGS)
        # TARGET_COMPILE_OPTIONS(${library_name} PUBLIC /bigobj)
    ENDIF()

    IF(${library_version} LESS 300)
        IF(APPLE)
            SET_TARGET_PROPERTIES(${library_name} PROPERTIES OUTPUT_NAME "mac.xpl")
        ELSEIF(UNIX)
            SET_TARGET_PROPERTIES(${library_name} PROPERTIES OUTPUT_NAME "lin.xpl")
        ELSEIF(WIN32)
            SET_TARGET_PROPERTIES(${library_name} PROPERTIES OUTPUT_NAME "win.xpl")
        ENDIF()
    ELSE()
        IF(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${DIRECTORY_PREFIX}")
            FILE(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${DIRECTORY_PREFIX}")
        ENDIF()
        SET_TARGET_PROPERTIES(${library_name} PROPERTIES OUTPUT_NAME "${DIRECTORY_PREFIX}/${library_name}.xpl")
    ENDIF()
    add_xplane_sdk_definitions(${library_name} ${library_version})

    SET_TARGET_PROPERTIES(${library_name} PROPERTIES PREFIX "")
    SET_TARGET_PROPERTIES(${library_name} PROPERTIES SUFFIX "")
    SET_TARGET_PROPERTIES(${library_name} PROPERTIES LINKER_LANGUAGE CXX)

    IF(APPLE)
        SET_PROPERTY(TARGET ${library_name} APPEND_STRING PROPERTY LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fPIC -fvisibility=hidden -bundle")
    ELSEIF(WIN32)
        #SET_PROPERTY(TARGET ${library_name} APPEND_STRING PROPERTY LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -undefined_warning -fPIC -fvisibility=hidden")
        TARGET_INCLUDE_DIRECTORIES("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/curl/include")
    ELSEIF(UNIX)
        SET_PROPERTY(TARGET ${library_name} APPEND_STRING PROPERTY LINK_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -rdynamic -nodefaultlibs -undefined_warning -fPIC -fvisibility=hidden")
    ENDIF()
        
    TARGET_LINK_LIBRARIES(${library_name} PUBLIC ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY} ${CEF_WRAPPER} ${CEF_LIBRARY} ${CURL_LIBRARY} ${OPENGL_LIBRARY} ${COCOA_LIBRARY} ${OPENSSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})
    #TARGET_LINK_LIBRARIES(${library_name} PUBLIC ${XPLM_LIBRARY} ${XPWIDGETS_LIBRARY})
    TARGET_INCLUDE_DIRECTORIES(${library_name} PUBLIC "${XPLANE_INCLUDES_PATH}/XPLM" "${XPLANE_INCLUDES_PATH}/Widgets" "${XPLANE_INCLUDES_PATH}/Wrappers")
    TARGET_INCLUDE_DIRECTORIES("${PROJECT_NAME}" PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/lib/${DIRECTORY_PREFIX}/cef")
    HEADER_DIRECTORIES(header_dir_list ${src_dir})
    TARGET_INCLUDE_DIRECTORIES(${library_name} PRIVATE ${header_dir_list})
        
ENDFUNCTION(add_xplane_plugin)

IF(NOT DEFINED SDK_VERSION)
    MESSAGE(FATAL_ERROR "SDK_VERSION not defined")
ENDIF()

add_xplane_plugin(avitab-browser ${SDK_VERSION} "${CMAKE_CURRENT_SOURCE_DIR}/src")
