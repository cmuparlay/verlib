## Jacob Nelson
## 
## These are bundle-specific macros to be included in Makefile.

## Bundle entry cleanup flags. Uncoment CLEANUP_BACKGROUND to 
## enable a background thread cleanup of bundle entries. The 
## CLEANUP_SLEEP macro adjusts how many nanoseconds the background 
## thread sleeps between iterations. It is required if 
## CLENAUP_BACKGROUND is enabled. CLEANUP_UPDATE is an experimental 
## configuration that allows update operations to reclaim stale 
## bundle entries
# FLAGS += -DBUNDLE_CLEANUP_BACKGROUND
# FLAGS += -DBUNDLE_CLEANUP_SLEEP=10000  # microseconds
# --------------------------

## Helpful flags for debugging.
# ---------------------------
# FLAGS += -DBUNDLE_CLEANUP_NO_FREE
# FLAGS += -DBUNDLE_DEBUG
# FLAGS += -DBUNDLE_PRINT_BUNDLE_STATS
# ---------------------------
# FLAGS += -DBUNDLE_CLEANUP_UPDATE
# ------------------------.
