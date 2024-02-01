
echo "Compiling Verlib data structures..."
mkdir -p build
cd build
cmake .. -DCMAKE_CXX_COMPILER=g++-10 -DDOWNLOAD_PARLAY=True
cd benchmark/verlib

mkdir -p graphs

make arttree_lf_noversion arttree_lf_versioned_hs arttree_lf_indirect_hs arttree_lf_noshortcut_hs btree_lf_noversion btree_lf_versioned_hs btree_lf_indirect_hs btree_lf_noshortcut_hs hash_block_cas_noversion hash_block_cas_versioned_hs hash_block_cas_indirect_hs hash_block_cas_noshortcut_hs dlist_lf_noversion dlist_lf_versioned_hs dlist_lf_indirect_hs dlist_lf_noshortcut_hs btree_lf_reconce_hs hash_block_cas_versioned_ls hash_block_cas_versioned_tl2s hash_block_cas_versioned_ws hash_block_cas_versioned_rs hash_block_cas_versioned_ns btree_lck_versioned_hs btree_lck_noversion arttree_lck_versioned_hs arttree_lck_noversion btree_lf_versioned_ls btree_lf_versioned_rs
cd ../../setbench
make srivastava_abtree_pub leis_olc_art
cd ../../

echo "Compiling Java benchmarks..."
cd rqsize_benchmark/java/
./compile
cd ..
echo "Finished compiling Java benchmarks"

echo "Compiling C++ benchmarks..."
cd cpp/microbench
make -j rqsize_exp

echo "Finished compiling C++ benchmarks"


