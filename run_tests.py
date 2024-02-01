import os

ds_list = ['arttree_lf_versioned_hs', 'arttree_lf_indirect_hs', 'arttree_lf_noshortcut_hs', 'btree_lf_versioned_hs', 'btree_lf_indirect_hs', 'btree_lf_noshortcut_hs', 'hash_block_cas_versioned_hs', 'hash_block_cas_indirect_hs', 'hash_block_cas_noshortcut_hs', 'dlist_lf_versioned_hs', 'dlist_lf_indirect_hs', 'dlist_lf_noshortcut_hs', 'btree_lf_reconce_hs', 'hash_block_cas_versioned_ls', 'hash_block_cas_versioned_tl2s', 'hash_block_cas_versioned_ws', 'hash_block_cas_versioned_rs', 'btree_lck_versioned_hs', 'arttree_lck_versioned_hs', 'btree_lf_versioned_ls', 'btree_lf_versioned_rs']

for ds in ds_list:
  os.system("echo \"testing: " + ds + "\"")
  os.system("./build/benchmark/verlib/" + ds + " -simple_test")