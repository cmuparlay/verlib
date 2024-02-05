#!/usr/bin/python3
#
# run all tests
#
import sys
sys.path.insert(1, '..')
from runtests import *

compile_clear("all_times")

class all_tests(parameters) :
    file_suffix = "flock"
    lists = ["list", "dlist"]
    list_sizes = [100, 1000]
    trees = ["hash_block", "arttree", "btree", "leaftree", "blockleaftree"]
    tree_sizes = [100000, 10000000]
    zipfians = [0, .99]
    mix_percents = [[5,0,0,0], [50,0,0,0]]
    suffixes_all = ["", "_nohelp"]

run_all(all_tests, "all_times")
