
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
update = ast.literal_eval(getArg("-u", "[0,5,20,50,100]"))
update = [int(up) for up in update]
tree_size = int(getArg("-s", 100000))
zipf = float(getArg("-z", 0.0))
if zipf == 0:
  zipf = int(zipf)
mfind_size = int(getArg("-mf", 16))
threads = int(getArg("-p", 4))
outfile = getArg("-f", "compare_timestamps")

exp1 = stamps_hash_update()

def setParameters(exp):
  exp.tree_sizes = [tree_size]
  exp.zipfians = [zipf]
  exp.mix_percents = []
  for up in update:
    exp.mix_percents.append([up,1,0,mfind_size])
  exp.processors = [threads]

setParameters(exp1)

results_file = "../../../timings/timestamps_" + hostname[0:5]

if os.path.exists(results_file):
  os.remove(results_file)

run_all(exp1, results_file)

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

print_update_graph(throughputs, parameters, ['hash_block_cas_versioned'], 
    ["ws", "rs", "tl2s", "ls", "hs", "ns"], tree_size, update, "mfind",mfind_size, zipf, threads, "-timestamp", outfile)