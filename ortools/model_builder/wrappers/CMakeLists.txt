file(GLOB _SRCS "*.h" "*.cc")
set(NAME ${PROJECT_NAME}_model_builder_wrappers)

# Will be merge in libortools.so
#add_library(${NAME} STATIC ${_SRCS})
add_library(${NAME} OBJECT ${_SRCS})
set_target_properties(${NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  )
target_include_directories(${NAME} PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_BINARY_DIR})
target_link_libraries(${NAME} PRIVATE
  absl::status
  protobuf::libprotobuf
  $<$<BOOL:${USE_SCIP}>:libscip>
  ${PROJECT_NAME}::proto)
#add_library(${PROJECT_NAME}::_model_builder_wrappers ALIAS ${NAME})
