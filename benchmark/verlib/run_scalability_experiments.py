
import sys
import ast
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
tree_size = int(getArg("-s", 100000))
zipf = float(getArg("-z", 0.0))
if zipf == 0:
  zipf = int(zipf)
mfind_size = int(getArg("-mf", 16))
threads = ast.literal_eval(getArg("-p", "[1,2,3,4]"))
threads = [int(up) for up in threads]
outfile = getArg("-f", "scalability")

exp1 = processor_scale()
exp2 = other()

def setParameters(exp):
  exp.tree_sizes = [tree_size]
  exp.zipfians = [zipf]
  exp.mix_percents = [[update,0,0,0]]
  exp.processors = threads

setParameters(exp1)
setParameters(exp2)

results_file = "../../../timings/scalability_" + hostname[0:5]

if os.path.exists(results_file):
  os.remove(results_file)

run_all(exp1, results_file)
run_all(exp2, results_file)

# generate graphs

throughputs = {}
parameters = {}

params = {'n': {'list' : 0, 'tree' : tree_size},
                'p': threads,
                'z': 0 if zipf == 0 else zipf, 
                'trans': 0,
                'range': 0}

readResultsFile(throughputs, parameters, results_file)

print(throughputs)
print(parameters)

print_scalability_graph(throughputs, parameters, ['btree_lf', 'btree_lck', 'srivastava_abtree_pub'], 
  ['noversion', 'versioned_hs'], tree_size, update,"find",0, zipf, threads, outfile)
print_scalability_graph(throughputs, parameters, ['arttree_lf', 'arttree_lck', 'leis_olc_art'], 
  ['noversion', 'versioned_hs'], tree_size, update,"find",0, zipf, threads, outfile)
