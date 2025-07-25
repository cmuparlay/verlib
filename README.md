# VERLIB: Concurrent Versioned Pointers

VERLIB is a C++ library supporting multiversioned pointers and lock-free locks as described in the paper:

VERLIB: Concurrent Versioned Pointers
Guy Blelloch and Yuanhao Wei
ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP 2024)

**The most recent version of VERLIB is now available as part of the [Fuse Library](https://github.com/cmuparlay/fuse#using-verlib).**

## Setting up and compiling
```
    python3 run_tests.py  # run sanity checks
```
  - All the binaries are already pre-compiled, but you can re-compile by running ```make clean``` inside the build directory and then running ```bash compile_all.sh``` in the top level directory
  - Expected output for the sanity checks can be found in: run_tests_expected_output.txt

## Running experiments and generating graphs
  - To reproduce all the graphs in the paper, run ```bash generate_graphs_from_paper.sh```
    - this command will take ~4 hours to run
  - The output graphs will be stored in the ```build/benchmark/verlib/graphs/``` directory and can be viewed by copying the directory out of the container using ```docker cp```
  - You can rerun a specific graph by running the corresponding command from the 
    generate_graphs_from_paper.sh file. Each command generates a single figure.
  - You can also run custom experiments (and generate graphs for them) using the following scripts from the ```build/benchmark/verlib/``` directory: 

```
    python3 run_optimization_experiments.py -u [update percent] -ls [list size] -s [tree/hashtable size] -z [zipfian parameter] -mf [multi-find size] -p [threads] -f [graph file name] -r [repeats] -t [runtime]
    python3 run_scalability_experiments.py ... [same as above]
    python3 run_size_experiments.py ... [same as above]
    python3 run_timestamp_experiments.py ... [same as above]
    python3 run_update_experiments.py ... [same as above]
    python3 run_zipf_experiments.py ... [same as above]
```
  - You can generate graphs similar to Figure 9 using the following script from the ```rqsize_benchmark``` directory:

```
    python3 run_rqsize_experiments.py [data structure size] [num update threads] [num range query threads] [list of range query sizes] ../build/benchmark/verlib/graphs/[graph file name] [repeats] [runtime] [JVM memory size, 50G recommended but lower works as well]
```

  - See generate_graphs_from_paper.sh for examples of how to use the above scripts and to see which figure each script corresponds to.
  - Parameter description: 
    - -p: number of threads
    - -s: initial size of tree/hashtable data structures
    - -ls: initial size of list data structures
    - -mf: number of keys to look up in a multi-find operation
    - -z: Zipfian parameter, number between [0, 1)
    - -u: percentage of updates, number between 0 and 100
    - -f: graph file name
    - -r: number of times to repeat each experiment
    - -t: runtime for each experiment (measured in seconds)
    - In some scripts, the parameter that gets varied expects a list of numbers as input (e.g. [1,2,3,4] with no spaces).



## List of claims from the paper supported by the artifact
  - Given a machine with ~128 logical cores, the graphs generated should be very similar to the ones reported in our paper
  - For machines with different numbers of cores, we recommend using the following settings to reproduce the general shape of our graphs:
    - Let X be the number of logical cores on your machine
    - experiments with 127 threads should be run with X-1 threads
    - scalability experiments should be run with [1, X/2, X, 1.5\*X, 2.5\*X, 3\*X] threads
