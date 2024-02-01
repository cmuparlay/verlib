
import sys
# sys.path.insert(1, './benchmark')
sys.path.insert(1, '../../../timings')
from rungraphs import *
from create_graphs_ppopp import *

def getOption(str) : 
    return sys.argv.count(str) > 0                              
                                                                                                    
def getArg(str, default) :                                            
  a = sys.argv          
  l = len(a)   
  for i in range(1, l) :                                               
    if (a[i] == str and  (i+1 != l)) :                 
        return sys.argv[i+1]                                           
  return default

# rounds = int(getArg("-r", 3))
# time = float(getArg("-t", 1.0))
update = int(getArg("-u", 20))
list_size = int(getArg("-ls", 100))
tree_size = int(getArg("-ts", 100000))
zipf = float(getArg("-z", 0.0))
mfind_size = int(getArg("-mf", 16))
threads = int(getArg("-p", 4))
outfile = getArg("-f", "compare_optimizations")

exp1 = versioning_across_structures()
exp2 = versioning_btree_reconce()

def setParameters(exp):
  exp.list_sizes = [list_size]
  exp.tree_sizes = [tree_size]
  exp.zipfians = [zipf]
  exp.mix_percents = [[update, 1, 0, mfind_size]]
  exp.processors = [threads]

setParameters(exp1)
setParameters(exp2)

results_file = "../../../timings/compare_" + hostname[0:5]

if os.path.exists(results_file):
  os.remove(results_file)

run_all(exp1, results_file)
run_all(exp2, results_file)

# generate graphs

throughputs = {}
parameters = {}

params = {'n': {'list' : list_size, 'tree' : tree_size},
                'p': threads,
                'z': 0 if zipf == 0 else zipf, 
                'trans': 0,
                'range': 0}

readResultsFile(throughputs, parameters, results_file)

print(throughputs)
print(parameters)

# print_table_timestamp_inc(throughputs, parameters, [update,'mfind',mfind_size], params)
print_table_mix_percent(throughputs, parameters, [update,'mfind',mfind_size], params, outfile)