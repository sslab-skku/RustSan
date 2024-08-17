llvm_map_components_to_libnames(llvm_libs bitwriter core ipo irreader instcombine instrumentation target linker analysis scalaropts support )
file (GLOB SOURCES
   pointer_stable.cc
   statistics.cc
)
add_executable(analyzer ${SOURCES})

target_link_libraries(analyzer ${SVF_LIB} ${llvm_libs})
target_link_libraries(analyzer ${Z3_LIBRARIES})
set_target_properties( analyzer PROPERTIES
                       RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )
