# Helpers for generating C++ sources from .proto (optionally with gRPC plugin)
#
# IMPORTANT: never use system /usr/bin/protoc when linking Conan protobuf -
# version skew produces incomplete types / broken grpc stubs.

function(_grpc_authz_resolve_protoc out_var)
  if(DEFINED protobuf_PACKAGE_FOLDER_RELEASE AND EXISTS "${protobuf_PACKAGE_FOLDER_RELEASE}/bin/protoc")
    set(_protoc "${protobuf_PACKAGE_FOLDER_RELEASE}/bin/protoc")
  elseif(DEFINED protobuf_PACKAGE_FOLDER AND EXISTS "${protobuf_PACKAGE_FOLDER}/bin/protoc")
    set(_protoc "${protobuf_PACKAGE_FOLDER}/bin/protoc")
  elseif(TARGET protobuf::protoc)
    get_target_property(_loc protobuf::protoc IMPORTED_LOCATION)
    if(NOT _loc)
      get_target_property(_loc protobuf::protoc IMPORTED_LOCATION_RELEASE)
    endif()
    set(_protoc "${_loc}")
  else()
    message(FATAL_ERROR "Cannot resolve Conan protoc executable")
  endif()

  if(NOT EXISTS "${_protoc}")
    message(FATAL_ERROR "protoc not found at ${_protoc}")
  endif()

  if(_protoc MATCHES "^/usr/bin/")
    message(FATAL_ERROR
      "Refusing system protoc (${_protoc}). "
      "It must come from the Conan protobuf package to match libprotobuf.")
  endif()

  set(${out_var} "${_protoc}" PARENT_SCOPE)
endfunction()

function(_grpc_authz_resolve_grpc_plugin out_var)
  if(TARGET gRPC::grpc_cpp_plugin)
    get_target_property(_plugin gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
    if(NOT _plugin)
      get_target_property(_plugin gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
    endif()
  endif()
  if(NOT _plugin)
    find_program(_plugin NAMES grpc_cpp_plugin REQUIRED)
  endif()
  set(${out_var} "${_plugin}" PARENT_SCOPE)
endfunction()

function(grpc_authz_add_proto_library target_name)
  set(options WITH_GRPC)
  set(oneValueArgs PROTO_ROOT)
  set(multiValueArgs PROTOS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARG_PROTO_ROOT)
    set(ARG_PROTO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  _grpc_authz_resolve_protoc(_protoc)
  if(ARG_WITH_GRPC)
    _grpc_authz_resolve_grpc_plugin(_grpc_plugin)
  endif()

  message(STATUS "grpc-authz-fuzz protoc: ${_protoc}")
  if(ARG_WITH_GRPC)
    message(STATUS "grpc-authz-fuzz grpc_cpp_plugin: ${_grpc_plugin}")
  endif()

  set(generated_srcs)
  set(generated_hdrs)

  foreach(proto_file IN LISTS ARG_PROTOS)
    get_filename_component(proto_abs "${proto_file}" ABSOLUTE)
    get_filename_component(proto_name "${proto_file}" NAME_WE)
    get_filename_component(proto_dir "${proto_abs}" DIRECTORY)
    file(RELATIVE_PATH proto_rel_dir "${ARG_PROTO_ROOT}" "${proto_dir}")

    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/${proto_rel_dir}")
    file(MAKE_DIRECTORY "${out_dir}")

    set(pb_cc "${out_dir}/${proto_name}.pb.cc")
    set(pb_h "${out_dir}/${proto_name}.pb.h")

    if(ARG_WITH_GRPC)
      set(grpc_cc "${out_dir}/${proto_name}.grpc.pb.cc")
      set(grpc_h "${out_dir}/${proto_name}.grpc.pb.h")
      add_custom_command(
        OUTPUT "${pb_cc}" "${pb_h}" "${grpc_cc}" "${grpc_h}"
        COMMAND ${_protoc}
                --proto_path=${ARG_PROTO_ROOT}
                --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
                --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
                --plugin=protoc-gen-grpc=${_grpc_plugin}
                ${proto_abs}
        DEPENDS ${proto_abs}
        COMMENT "Generating C++/gRPC sources for ${proto_file}"
        VERBATIM
      )
      list(APPEND generated_srcs "${pb_cc}" "${grpc_cc}")
      list(APPEND generated_hdrs "${pb_h}" "${grpc_h}")
    else()
      add_custom_command(
        OUTPUT "${pb_cc}" "${pb_h}"
        COMMAND ${_protoc}
                --proto_path=${ARG_PROTO_ROOT}
                --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
                ${proto_abs}
        DEPENDS ${proto_abs}
        COMMENT "Generating C++ protobuf sources for ${proto_file}"
        VERBATIM
      )
      list(APPEND generated_srcs "${pb_cc}")
      list(APPEND generated_hdrs "${pb_h}")
    endif()
  endforeach()

  add_library(${target_name} ${generated_srcs} ${generated_hdrs})
  target_include_directories(${target_name} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(${target_name} PUBLIC protobuf::libprotobuf)
  if(ARG_WITH_GRPC)
    target_link_libraries(${target_name} PUBLIC gRPC::grpc++)
  endif()
endfunction()
