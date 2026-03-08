# Helper function to recursively set FOLDER property for all targets in a directory
function(yspeech_set_folder_for_targets_in_dir dir folder_name)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

    foreach(target IN LISTS targets)
        get_target_property(target_type ${target} TYPE)
        if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
            set_target_properties(${target} PROPERTIES FOLDER "${folder_name}")
        endif()
    endforeach()

    foreach(subdir IN LISTS subdirs)
        yspeech_set_folder_for_targets_in_dir("${subdir}" "${folder_name}")
    endforeach()
endfunction()
