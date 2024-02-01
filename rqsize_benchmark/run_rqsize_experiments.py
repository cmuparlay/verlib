
import sys 
import os
import socket

sys.path.insert(1, 'internals/')

import create_graphs as graph

if len(sys.argv) == 1 or sys.argv[1] == '-h':
  print("Usage: python3 run_rqsize_experiments.py [num_keys] [update_threads] [rq_threads] [rqsizelist] [outputfile] [num_repeats] [runtime] [JVM memory size]")
  print("For example: python3 run_rqsize_experiments.py 10000 4 4 [8,256] graph.png 1 1 1G")
  exit(0)


num_keys = sys.argv[1]
update_threads = sys.argv[2]
rq_threads = sys.argv[3]
rqsizes = sys.argv[4][1:-1].split(',')
graphfile = sys.argv[5]
repeats = int(sys.argv[6])
runtime = sys.argv[7]
JVM_mem_size = sys.argv[8]
graphs_only = False
if len(sys.argv) == 10:
  graphs_only = True

ins=50
rmv=50
th = int(update_threads)+int(rq_threads)
key_range = int(num_keys)*2

graphtitle = "rqsize: " + str(num_keys) + "keys-" + str(update_threads) + "up-" + str(rq_threads) + "rq"

benchmark_name = graphtitle.replace(':','-').replace(' ', '-')
cpp_results_file_name = "cpp/results/" + benchmark_name + ".txt"
java_results_file_name = "java/results/" + benchmark_name + ".csv"
verlib_results_file_name = "verlib_results/" + benchmark_name + ".txt"
# print(results_file_name)

cpp_datastructures = ["bst.rq_lockfree.out", "skiplistlock.rq_bundle.out", "citrus.rq_bundle.out"]
java_datastructures = ["LFCA", "Jiffy"]
verlib_datastructures = ["btree_lf_versioned_ls", "btree_lf_versioned_hs", "btree_lf_versioned_rs"] 
# verlib_datastructures = []

if not graphs_only:
  jemalloc = "LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so "
  numactl = "numactl -i all "

  # run verlib data structures
  cmdbase = "./../build/benchmark/verlib/"

  os.system("rm -rf verlib_results/*.out")

  i = 0
  for ds in verlib_datastructures:
    for rqsize in rqsizes:
      i = i+1
      tmp_results_file = "verlib_results/data-trials" + str(i) + ".out"
      cmd = numactl + cmdbase + ds + " -u " + str(ins+rmv) + " -n " + str(num_keys) + " -rs " + str(int(int(rqsize)/2)) + " -tt " + runtime + " -p " + str(th) + " -rqthreads " + str(rq_threads) + " -id " + " -r " + str(repeats) + " -dense"
      print(cmd)
      f = open(tmp_results_file, "w")
      f.write(cmd + '\n')
      f.close()
      if os.system(cmd + " >> " + tmp_results_file) != 0:
        print("")
        exit(1)

  # run C++ data structures
  cmdbase = "./cpp/microbench/" + socket.gethostname() + "."

  os.system("rm -rf cpp/microbench/results/*.out")

  i = 0
  for ds in cpp_datastructures:
    for rqsize in rqsizes:
      for j in range(int(repeats)):
        i = i+1
        tmp_results_file = "cpp/microbench/results/data-trials" + str(i) + ".out"
        cmd = jemalloc + numactl + cmdbase + ds + " -i " + str(ins) + " -d  " + str(rmv) + " -k " + str(key_range) + " -rq 0 -rqsize " + rqsize + " -t " + str(int(runtime)*1000) + " -p -nrq " + str(rq_threads) + " -nwork " + str(update_threads)
        print(cmd)
        f = open(tmp_results_file, "w")
        f.write(cmd + '\n')
        f.close()
        if os.system(cmd + " >> " + tmp_results_file) != 0:
          print("")
          exit(1)

  # run Java data structures
  cmdbase = "java -server -Xms" + JVM_mem_size + " -Xmx" + JVM_mem_size + " -Xbootclasspath/a:'java/lib/scala-library.jar:java/lib/deuceAgent.jar' -jar java/build/experiments_instr.jar "

  # delete previous results
  os.system("rm -rf java/build/*.csv")
  os.system("rm -rf java/build/*.csv_stdout")

  i = 0
  for ds in java_datastructures:
    for rqsize in rqsizes:
      i = i+1
      cmd = numactl + cmdbase + str(th) + " " + str(repeats*2) + " " + runtime + " " + ds + " -ins" + str(ins) + " -del" + str(rmv) + " -rq0 -rqsize" + str(rqsize) + " -rqers" + rq_threads + " -keys" + str(key_range) + " -prefill -file-java/build/data-trials" + str(i) + ".csv"
      print(cmd)
      if os.system(cmd) != 0:
        print("")
        exit(1)

  os.system("cat java/build/data-*.csv > " + java_results_file_name)
  os.system("cat cpp/microbench/results/data-*.out > " + cpp_results_file_name)
  os.system("cat verlib_results/data-*.out > " + verlib_results_file_name)

graph.plot_combined_rqsize_graphs(cpp_results_file_name, java_results_file_name, verlib_results_file_name, graphfile, graphtitle)
