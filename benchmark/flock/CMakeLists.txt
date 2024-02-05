set(SOURCES ../test_sets.cpp)
copy_bench_file("run_tests.py")
copy_bench_file("run_sanity_checks.py")
	
set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
# "avltree"
# "list_onelock", "hash"
set(FLOCK_BENCH "leaftree" "blockleaftree" "arttree" "btree" "dlist" "list" "hash_block")
set(FLOCK_BENCH_USE_CAS "hash_block")

set(OTHER_LIST_DIR ${PROJECT_SOURCE_DIR}/ds)
set(OTHER_LIST_BENCH "harris_list" "harris_list_opt")

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE flock)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

#foreach(bench ${OTHER_LIST_BENCH})
#  add_benchmark(${bench} ${OTHER_LIST_DIR}/${bench} "")
#endforeach()

foreach(bench ${FLOCK_BENCH})
  add_benchmark(${bench} ${STRUCT_DIR}/${bench} "")
  add_benchmark(${bench}_nohelp ${STRUCT_DIR}/${bench} "NoHelp")
endforeach()

# None of these need helping
foreach(bench ${FLOCK_BENCH_USE_CAS})
  add_benchmark(${bench}_lf ${STRUCT_DIR}/${bench} "NoHelp;UseCAS")
  add_benchmark(${bench}_lf_nohelp ${STRUCT_DIR}/${bench} "UseCAS")
endforeach()
