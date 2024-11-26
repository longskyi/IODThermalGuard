if(CMAKE_COMPILER_IS_GNUCXX)
    # GCC
    target_compile_options(${PROJECT_NAME}
        PRIVATE -finput-charset=UTF-8
        PRIVATE -fexec-charset=UTF-8
    )
elseif(MSVC)
    # MSVC
    target_compile_options(${PROJECT_NAME}
        PRIVATE /utf-8
    )
endif()