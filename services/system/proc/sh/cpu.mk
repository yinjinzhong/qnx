# List of files that we want to compile at specific 
# optimization levels.

# Default for ker is O2
CCFLAGS_ker_sh += -O2

# If I need to specify the other bits to be a certain level
CCFLAGS_memmgr_sh += -O1
CCFLAGS_pathmgr_sh += -O1
CCFLAGS_proc_sh +=  -O1
CCFLAGS_procmgr_sh += -O1

# Certain files don't follow the general rules
CCFLAGS_ker_timer_sh += -O1
CCFLAGS_pathmgr_init_sh += -O2
CCFLAGS_pathmgr_link_sh += -O2
CCFLAGS_pathmgr_node_sh += -O2
CCFLAGS_pathmgr_object_sh += -O2
CCFLAGS_pathmgr_open_sh += -O2
