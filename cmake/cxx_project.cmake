function(cxx_project_preset)
    foreach(target IN ITEMS ${ARGN})
        set_target_properties(${target} PROPERTIES 
            CXX_STANDARD 20
            C_VISIBILITY_PRESET hidden
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
            POSITION_INDEPENDENT_CODE ON
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
        )
    endforeach()
endfunction()

function(cxx_components_project_preset)
    foreach(target IN ITEMS ${ARGN})
        cxx_project_preset(${target})
        target_include_directories(${target} PRIVATE
            ../../main/include
        )
    endforeach()
endfunction()