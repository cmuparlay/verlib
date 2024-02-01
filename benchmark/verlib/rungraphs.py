#!/usr/bin/python3
#
# run all tests
#
import sys
sys.path.insert(1, '..')
from runtests import *

# *****************
# versioning type experiments
# (i.e. non versioned, vs indirect, vs indirection-on-need ..)
# *****************

# bar graph
# grouped by data structure (arttree, btree, hash_block, dlist)
# within each one bar for each of noversioning, versioned, indirect, and no shortcut
# for each use lock free (or cas for hash table) and lazy stamps
class versioning_across_structures(parameters) :
    file_suffix = "paper"
    lists = ["dlist_lf"]
    list_sizes = [1000]
    trees = ["arttree_lf", "btree_lf", "hash_block_cas"]
    tree_sizes = [10000000]
    zipfians = [0]
    mix_percents = [[20,1,0,16]]
    suffixes_all = ["_noversion", "_versioned_hs", "_indirect_hs", "_noshortcut_hs"]

# run_all(versioning_across_structures)

# add to above just for btrees
class versioning_btree_reconce(parameters) :
    file_suffix = "paper"
    lists = []
    trees = ["btree_lf_reconce_hs"]
    tree_sizes = [10000000]
    zipfians = [0]
    mix_percents = [[20,1,0,16]]

# run_all(versioning_btree_reconce)

# graph
# for lock-free arttree with lazy stamps, n = 10M, zipfian=0, rs=16
# x-axis: update percents from 0 to 100
# y-axis: mops
# one line for each of noversioning, versioned, indirect, and no shortcut
class versioning_arttree_update(parameters) :
    file_suffix = "paper"
    trees = ["arttree_lf"]
    tree_sizes = [10000000]
    zipfians = [0]
    mix_percents = [[0,1,0,16],[5,1,0,16],[20,1,0,16],[50,1,0,16],[100,1,0,16]]
    suffixes_all = ["_noversion", "_versioned_hs", "_indirect_hs", "_noshortcut_hs"]
    
# run_all(versioning_arttree_update)

# graph
# for lock-free arttree with lazy stamps, n = 10M, update=20%, rs=16
# x-axis: zipfian from 0 to .99
# y-axis: mops
# one line for each of noversioning, versioned, indirect, and no shortcut
class versioning_arttree_zipfian(parameters) :
    file_suffix = "paper"
    trees = ["arttree_lf"]
    tree_sizes = [10000000]
    zipfians = [0,.2,.4,.6,.8,.9,.99]
    mix_percents = [[20,1,0,16]]
    suffixes_all = ["_noversion", "_versioned_hs", "_indirect_hs", "_noshortcut_hs"]

# run_all(versioning_arttree_zipfian)

# graph
# for lock-free arttree with lazy stamps, zipfian = 0, update=20%, rs=16
# x-axis: sizes from 10K to 100M
# y-axis: mops
# one line for each of noversioning, versioned, indirect, and no shortcut
class versioning_arttree_size(parameters) :
    file_suffix = "paper"
    trees = ["arttree_lf"]
    tree_sizes = [10000,1000000,1000000,10000000,100000000]
    zipfians = [0]
    mix_percents = [[20,1,0,16]]
    suffixes_all = ["_noversion", "_versioned_hs", "_indirect_hs", "_noshortcut_hs"]
    
# run_all(versioning_arttree_size)

# *****************
# time stamp experiments
# *****************

# # bar graph
# # grouped by data structure (arttree, btree, hash_block, dlist)
# # within each one bar for each of lazy stamp, tl2 stamp, hardware stamp, write stamp, read stamp and no stamp
# # for each use versioned, zipfian = 0, rs=16, u=20%, n = 10M
# class stamps_across_structures(parameters) :
#     file_suffix = "paper"
#     lists = ["dlist_lf_versioned"]
#     list_sizes = [1000]
#     trees = ["arttree_lf_versioned", "btree_lf_versioned", "hash_block_cas_versioned"]
#     tree_sizes = [10000000]
#     zipfians = [0]
#     mix_percents = [[20,1,0,16]]
#     suffixes_all = ["_ls", "_tl2s", "_hs", "_ws", "_rs", "_ns"]

# run_all(stamps_across_structures)

# graph
# for cas hash_block with zipfian = 0, rs=16, n = 10M
# x-axis: update percents from 0-100
# y-axis: mops
# one line for each of lazy stamp, tl2 stamp, hardware stamp, write stamp, read stamp and no stamp
class stamps_hash_update(parameters) :
    file_suffix = "paper"
    trees = ["hash_block_cas_versioned"]
    tree_sizes = [10000000]
    zipfians = [0]
    mix_percents = [[0,1,0,16],[5,1,0,16],[20,1,0,16],[50,1,0,16],[100,1,0,16]]
    suffixes_all = ["_ls", "_tl2s", "_hs", "_ws", "_rs", "_ns"]

# run_all(stamps_hash_update)

# graph
# for cas hash_block with update=20%, rs=16, n = 10M
# x-axis: zipfians from 0 to .99
# y-axis: mops
# one line for each of lazy stamp, tl2 stamp, hardware stamp, write stamp, read stamp and no stamp
class stamps_hash_zipfian(parameters) :
    file_suffix = "paper"
    trees = ["hash_block_cas_versioned"]
    tree_sizes = [10000000]
    zipfians = [0,.5,.75,.9,.95,.99]
    mix_percents = [[20,1,0,16]]
    suffixes_all = ["_ls", "_tl2s", "_hs", "_ws", "_rs", "_ns"]

# run_all(stamps_hash_zipfian)

# graph
# for cas hash_bl with update=20%, zipfian=0, n = 10M
# x-axis: rs from 1 to 64
# y-axis: mops
# one line for each of lazy stamp, tl2 stamp, hardware stamp, write stamp, read stamp and no stamp
class stamps_hash_mfind_size(parameters) :
    file_suffix = "paper"
    trees = ["hash_block_cas_versioned"]
    tree_sizes = [10000000]
    zipfians = [0]
    mix_percents = [[20,1,0,1],[20,1,0,2],[20,1,0,4],[20,1,0,8],[20,1,0,16],[20,1,0,32],[20,1,0,64]]
    suffixes_all = ["_ls", "_tl2s", "_hs", "_ws", "_rs", "_ns"]

# run_all(stamps_hash_mfind_size)

# graph
# for versioned btree , n = 10M, z=0, p=100,
# x-axis: rs from 4 to 16K
# y-axis: mops
# one line for each of hardware stamp, lazy stamp, and read stamp
# to be combined with other range search data structures
class range_search(parameters) :
    file_suffix = "paper"
    trees = ["btree_lf_versioned"]
    tree_sizes = [10000000]
    zipfians = [0]
    processors = [100]
    mix_percents = [[100,0,95,4], [100,0,95,64], [100,0,95,1024], [100,0,95,16384]]
    suffixes_all = ["_hs", "_ls", "_rs"]

# run_all(range_search)

# 2 graphs
# for versioned btree and versioned arttree
# x-axis: processors from 1 to 384
# y-axis: mops
# one line for each of {lock free, locked} with ls versioning, and with no versioning
# to be combined with abtrees and leistrees
class processor_scale(parameters) :
    file_suffix = "paper"
    lists = []
    trees = ["btree", "arttree"]
    tree_sizes = [10000000]
    zipfians = [.99]
    mix_percents = [[5,0,0,0]]
    processors = [1,64,128,192,256,320,384]
    suffixes_all = ["_lf_versioned_hs", "_lck_versioned_hs","_lf_noversion", "_lck_noversion"]

# run_all(processor_scale)

# 2 graphs
# for srivastava btree and leis arttree
# x-axis: processors from 1 to 384
# y-axis: mops
# to be combined with above
class other(parameters) :
    file_suffix = "paper"
    lists = []
    trees = ["../../setbench/srivastava_abtree_pub", "../../setbench/leis_olc_art"]
    tree_sizes = [10000000]
    zipfians = [.99]
    mix_percents = [[5,0,0,0]]
    processors = [1,64,128,192,256,320,384]
    suffixes_all = [""]

# run_all(other)