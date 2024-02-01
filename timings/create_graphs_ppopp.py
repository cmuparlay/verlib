
import os
import re
from tabulate import tabulate
import matplotlib as mpl
import matplotlib.pyplot as plt
import numpy as np
import math

mpl.rcParams['grid.linestyle'] = ":"
# mpl.rcParams.update({'font.size': 35})
mpl.rcParams.update({'font.size': 20})
mpl.rcParams['pdf.fonttype'] = 42
mpl.rcParams['ps.fonttype'] = 42

# input_files = ["ip-172-31-45-236_10_25_22", "ip-172-31-40-178_10_25_22"]
# input_files = ["scalability_aware.aladdin.cs.cmu.edu_01_05_23"]
# input_files = ["ip-17_paper_08_03_23", "ip-17_paper_08_04_23"] # ppopp version
# input_files = ["ip-17_parlay_10_22_23"] 
# output_folder = "aug3" # ppopp version
output_folder = "graphs/"

param_list = ['ds','per','query_type','up','range','rs','trans','n','p','z']
ds_list = ["arttree_lf", "btree_lf", "arttree_lck", "btree_lck", "dlist_lf", "list_lf", "hash_block_cas"]
ds_list_other = ["srivastava_abtree_pub", "leis_olc_art"]

num_tables = 0

combined_output = ""

# names = { 
#           'btree_lf_versioned_hs': 'btree-lockfree-versioned',
#           'btree_lck_versioned_hs': 'btree-blocking-versioned',
#           'btree_lck_noversion' : 'btree-blocking',
#           'btree_lf_noversion' : 'btree-lockfree',
#           'srivastava_abtree_pub': 'srivastava_abtree',

#           'arttree_lf_versioned_hs' : 'arttree-lockfree-versioned',
#           'arttree_lck_versioned_hs' : 'arttree-blocking-versioned',
#           'arttree_lck_noversion' : 'arttree-blocking',
#           'arttree_lf_noversion' : 'arttree-lockfree',
#           'leis_olc_art' : 'leis_artree',
# }

names = { 
          'btree_lf_versioned_hs': 'btree-lockfree-versioned',
          'btree_lck_versioned_hs': 'btree-blocking-versioned',
          'btree_lck_noversion' : 'btree-blocking',
          'btree_lf_noversion' : 'btree-lockfree',
          'srivastava_abtree_pub': 'srivastava_abtree',

          'arttree_lf_versioned_hs' : 'arttree-lockfree-versioned',
          'arttree_lck_versioned_hs' : 'arttree-blocking-versioned',
          'arttree_lck_noversion' : 'arttree-blocking',
          'arttree_lf_noversion' : 'arttree-lockfree',
          'leis_olc_art' : 'leis_artree',
}

colors = {
          'btree_lf_versioned_hs': 'C0',
          'btree_lck_versioned_hs': 'C1',
          'btree_lck_noversion' : 'C2',
          'btree_lf_noversion' : 'C3',
          'srivastava_abtree_pub': 'C4',

          'arttree_lf_versioned_hs' : 'C0',
          'arttree_lck_versioned_hs' : 'C1',
          'arttree_lck_noversion' : 'C2',
          'arttree_lf_noversion' : 'C3',
          'leis_olc_art' : 'C4',
}
linestyles = {
          'btree_lf_versioned_hs': '-',
          'btree_lck_versioned_hs': '-',
          'btree_lck_noversion' : '--',
          'btree_lf_noversion' : '--',
          'srivastava_abtree_pub': '--',

          'arttree_lf_versioned_hs' : '-',
          'arttree_lck_versioned_hs' : '-',
          'arttree_lck_noversion' : '--',
          'arttree_lf_noversion' : '--',
          'leis_olc_art' : '--',
}
markers =    {
          'btree_lf_versioned_hs': 's',
          'btree_lck_versioned_hs': '>',
          'btree_lck_noversion' : 'o',
          'btree_lf_noversion' : 'x',
          'srivastava_abtree_pub': '|',

          'arttree_lf_versioned_hs' : 's',
          'arttree_lck_versioned_hs' : '>',
          'arttree_lck_noversion' : 'o',
          'arttree_lf_noversion' : 'x',
          'leis_olc_art' : '|',
}

names2 = {
  'arttree_lf_versioned_hs': 'IndOnNeed',
  'arttree_lf_noversion' : 'NonVersioned',
  'arttree_lf_indirect_hs' : 'Indirect',
  'arttree_lf_noshortcut_hs' : 'NoShortcut',

  'hash_block_cas_versioned_ws' : 'updateTS',
  'hash_block_cas_versioned_rs' : 'queryTS',
  'hash_block_cas_versioned_tl2s' : 'TL2-TS',
  'hash_block_cas_versioned_ls' : 'optTS',
  'hash_block_cas_versioned_hs' : 'hwTS',
  'hash_block_cas_versioned_ns' : 'NoStamp',
}

colors2 =    {
          'arttree_lf_versioned_hs': 'C2',
          'arttree_lf_noversion': 'C4',
          'arttree_lf_indirect_hs' : 'C0',
          'arttree_lf_noshortcut_hs' : 'C1',

  'hash_block_cas_versioned_ws' : 'C0',
  'hash_block_cas_versioned_rs' : 'C1',
  'hash_block_cas_versioned_tl2s' : 'C2',
  'hash_block_cas_versioned_ls' : 'C3',
  'hash_block_cas_versioned_hs' : 'C4',
  'hash_block_cas_versioned_ns' : 'C5',
}

markers2 =    {
          'arttree_lf_versioned_hs': 's',
          'arttree_lf_noversion': '>',
          'arttree_lf_indirect_hs' : 'o',
          'arttree_lf_noshortcut_hs' : 'x',

  'hash_block_cas_versioned_ws' : 's',
  'hash_block_cas_versioned_rs' : '>',
  'hash_block_cas_versioned_tl2s' : 'o',
  'hash_block_cas_versioned_ls' : 'x',
  'hash_block_cas_versioned_hs' : '|',
  'hash_block_cas_versioned_ns' : '*',
}

# try to get LCFA running: https://github.com/kboyles8/lock-free-search-tree
# https://www.scienceopen.com/hosted-document?doi=10.14293/S2199-1006.1.SOR-.PPIV7TZ.v1
# compare with LFCA and MRLOCK


def custom_round(num):
  if num >= 100:
    return round(num)
  elif num >= 10:
    return round(num, 1)
  elif num >= 1:
    return round(num, 2)
  else:
    return round(num, 3)

def splitdsname(name):
  for ds in ds_list_other:
    if ds in name:
      return ds, ''
  for ds in ds_list:
    if name == ds:
      return ds, 'non_per'
    if name.startswith(ds):
      return ds, name.replace(ds+'_','')

def toString(param):
  ds_name = param['ds']
  if param['per'] != 'non_per' and param['per']!='*' and param['per']!='':
    ds_name += '_' + param['per']
  if param['per'] == 'ro' and param['ds'] == 'list':
    return "invalid key"
  size = param['n']
  if type(param['n']) is dict and param['per'] != '*':
    if 'list' in param['ds']:
      size = param['n']['list']
    else:
      size = param['n']['tree']
  return ds_name + ',' + str(param['up']) + '%update,' + str(param['range']) + '%range,' + str(param['query_type']) + ',rs=' + str(param['rs']) + ',n=' + str(size) + ',p=' + str(param['p'])  + ',z=' + str(param['z'])

def toParam(string):
  string = string.replace('../', '').replace('setbench/','')
  ds, per = splitdsname(string.split(',')[0])

  string = ','.join(string.split(',')[1:])
  numbers = re.sub('[^0-9.]', ' ', string).split()
  
  param = {'ds' : ds, 'per': per}
  if 'mfind' in string:
    param['query_type'] = 'mfind'
  else:
    param['query_type'] = 'find'
  p = param_list[3:]
  for i in range(len(p)):
    if '.' in numbers[i]:
      param[p[i]] = float(numbers[i])
    else:
      param[p[i]] = int(numbers[i])
  return param

# mutates throughputs and parameters
def readResultsFile(throughputs, parameters, resultsfile):
  throughputs_raw = {}
  param = {}
  for filename in [resultsfile]:
    for line in open(filename, "r"):
      if "update_throughput" in line:
        continue
      if not line.startswith('./'):
        continue
      param = toParam(line[2:])
      key = toString(param)
      val_str = line.split(',')[-1]
      val = float(line.split(',')[-1])
      if line[2:].replace('trans=0,','').replace('../', '').replace('setbench/','').strip() != (key + "," + val_str).strip():
        print(line[2:].replace('trans=0,','').replace('../', '').replace('setbench/',''))
        print(key + "," + str(val))
        input()
      if key not in throughputs_raw:
        throughputs_raw[key] = [val]
      else:
        throughputs_raw[key].append(val)
      for p in param_list:
        if p not in parameters:
          parameters[p] = [param[p]]
        elif param[p] not in parameters[p]:
          parameters[p].append(param[p])
  for key in throughputs_raw:
    throughputs[key] = sum(throughputs_raw[key])/len(throughputs_raw[key])
  for p in param_list:
    parameters[p].sort()

def transpose(data_):
  data = []
  for x in data_[0]:
    data.append([])

  for r in range(len(data_)):
    for c in range(len(data_[0])):
      data[c].append(data_[r][c])
  return data


colors_list = ['C0', 'C1', 'C2', 'C3', 'C4', 'C5', 'C6']

def export_legend(legend, filename="legend.pdf"):
    fig  = legend.figure
    fig.canvas.draw()
    bbox  = legend.get_window_extent().transformed(fig.dpi_scale_trans.inverted())
    fig.savefig(filename, dpi="figure", bbox_inches=bbox)

def plot_bar_graph(data_, headers, title, x_axis, params={},colnamedict={},graph_type="",outfile="default"):
  x_labels = []
  for r in range(len(data_)):
    is_list = False
    for c in range(len(data_[0])):
      if c == 0 or data_[r][c] == '-':
        if c == 0:
          if data_[r][c] == 'list_lf':
            x_labels.append('list\n(10x)')
            is_list = True
          elif 'dlist_lf' in data_[r][c]:
            x_labels.append('dlist(10x)')
            is_list = True
          elif 'hash_block_cas' in data_[r][c]:
            x_labels.append('hash')
          else:
            x_labels.append((data_[r][c].replace('_lf_versioned', '')))
        data_[r][c] = 0
      elif is_list:
        # print("true")
        data_[r][c] = data_[r][c]*10
  data = np.transpose(np.array(data_, dtype=np.uint32))[1:]
  # data = [x if x != '-' else 0 for x in transpose(data_)[1:]]
  # print(data)
  barWidth = 1/(len(data)+3)
  fig = plt.subplots(figsize =(12, 8))
  # Set position of bar on X axis
  X = np.arange(len(data[0]))

  for i in range(len(data)):
    name = headers[i+1]
    for key in colnamedict.keys():
      if name in key:
        name = colnamedict[name]
        break
    
    plt.bar(X + i*barWidth, data[i], color=colors_list[i], width = barWidth,
        edgecolor ='grey', label=name)

  if not bool(params):
    plt.xlabel('Operation mix')
  # else:
    # plt.xlabel('Data structure')
  plt.ylabel('Throughput (Mop/s)')
  plt.xticks([r + barWidth*(len(data)-1)/2 for r in range(len(data[0]))], x_labels)

  plt.title(title)

  plt.tight_layout()

  if graph_type == "":
    plt.legend()
  else:
    legend_x = 1
    legend_y = 0.5 
    plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  outfilename = title
  if bool(params):
    outfilename = str(params['up']) + 'up_' + str(params['query_type']) + '_' + str(params['rs']) + 'rs_' + str(params['z']) + 'z_' + str(params['n']['tree']) + 'n' + graph_type
  plt.savefig(output_folder + '/' + outfile + ".png", bbox_inches='tight')
  # legend_x = 1
  # legend_y = 0.5 
  # legend = plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y), ncol=7, framealpha=0.0)
  # if bool(params):
  #   export_legend(legend, output_folder+'/optimization-legend.pdf')
  # else:
  #   export_legend(legend, output_folder+'/timestamp-legend.pdf')
  plt.close('all')


def print_table(throughput, parameters, row, col, params, rowvals=[], colvals=[],colnamedict={},graph_type="",outfile="default"):
  p = params.copy()
  p[row] = '*'
  p[col] = '*'
  title = toString(p)
  title = str(params['up']) + 'up_' + str(params['query_type']) + '_' + str(params['rs']) + 'rs_' + str(params['z']) + 'z_' + str(params['n']['tree']) + 'n' + graph_type
  output = title + '\n========================================= \n\n'
  f = open(output_folder + '/' + outfile + ".txt", "w")
  if rowvals == []:
    rowvals = parameters[row]
  # if 'hash_block' in rowvals:
  #   rowvals.remove('hash_block')
  if colvals == []:
    colvals = parameters[col]
  headers = ['ds'] + ['direct' if x == 'ro' else x for x in colvals]
  data = []
  for r in rowvals:
    row_data = [r]
    for c in colvals:
      p[row] = r
      p[col] = c
      # print(toString(p))
      if toString(p) in throughput:
        row_data.append(custom_round(throughput[toString(p)]))
      else:
        print(toString(p))
        row_data.append('-')
    data.append(row_data)
  output += tabulate(data, headers=headers) + '\n'
  global num_tables, combined_output
  num_tables += 1
  combined_output += output + '\n'
  print(output)
  print()
  f.write(output)
  f.close()
  plot_bar_graph(data, headers, title, "Data Structure", p, colnamedict, graph_type,outfile)

def print_table_mix_percent(throughput, parameters, mix_percent, params, outfile):
  params['up'] = mix_percent[0]
  # params['mfind'] = mix_percent[1]
  params['query_type'] = mix_percent[1]
  params['rs'] = mix_percent[2]
  rowvals = []
  for ds in ds_list:
    if ds in parameters['ds'] and 'lck' not in ds:
      rowvals.append(ds)
  # rowvals = parameters['ds']
  colnames  = ['Indirect', 'NoShortcut', 'IndOnNeed', 'RecOnce', 'Non-versioned']
  colvals = ['indirect_hs', 'noshortcut_hs', 'versioned_hs', 'reconce_hs', 'noversion']
  colnamedict = {}
  for i in range(len(colvals)):
    colnamedict[colvals[i]] = colnames[i]
  print_table(throughput, parameters, 'ds', 'per', params, rowvals, colvals, colnamedict, "", outfile)

def print_table_timestamp_inc(throughput, parameters, mix_percent, params, outfile):
  params['up'] = mix_percent[0]
  # params['mfind'] = mix_percent[1]
  params['query_type'] = mix_percent[1]
  params['rs'] = mix_percent[2]
  rowvals = ["dlist_lf_versioned", "arttree_lf_versioned", "btree_lf_versioned", "hash_block_cas_versioned"]
  # rowvals = parameters['ds']
  colnames  = ["updateTS", "queryTS", "TL2-TS", "optimisticTS", "hardwareTS", "No-Stamp"]
  colvals = ["ws", "rs", "tl2s", "ls", "hs", "ns"]
  colnamedict = {}
  for i in range(len(colvals)):
    colnamedict[colvals[i]] = colnames[i]
  print_table(throughput, parameters, 'ds', 'per', params, rowvals, colvals, colnamedict,"_timestamp",outfile)

# def print_table_timestamp_inc(throughput, parameters, ds, per, size, mix_percents, params):
#   p = params.copy()
#   p['ds'] = ds
#   p['n'] = size
#   title = 'inc_policy_' + ds + '_' + per + '_size_' + str(size) + '_z_' + str(params['z'])
#   output = title + '\n========================================= \n\n'
#   f = open(output_folder + '/' + title + ".txt", "w")

#   colvals = ['_rs', '_ws', '_ls', '_hw', 'non_per']
#   headers = ['workload (up-mfind-range-rs)', 'queryTS', 'updateTS', 'optTS', 'hwTS', 'Non-versioned']
#   data = []
#   for mix_percent in mix_percents:
#     row_data = [str(mix_percent[0]) + "-" + str(mix_percent[1]) + '-' + str(mix_percent[3])]
#     for c in colvals:
#       p['per'] = per + c
#       if c == 'non_per': 
#         p['per'] = c
#       p['up'] = mix_percent[0]
#       p['mfind'] = mix_percent[1]
#       p['range'] = mix_percent[2]
#       p['rs'] = mix_percent[3]
#       # print(p)
#       # print(toString(p))
#       if toString(p) in throughputs:
#         row_data.append(custom_round(throughputs[toString(p)]))
#       else:
#         row_data.append('-')
#     data.append(row_data)
#   output += tabulate(data, headers=headers) + '\n'
#   global num_tables, combined_output
#   num_tables += 1
#   combined_output += output + '\n'
#   print(output)
#   f.write(output)
#   f.close()
#   plot_bar_graph(data, headers, title, "Operation mix (updates-mfinds-querysize)")

def print_scalability_graph(throughput, parameters, ds, per, size, up, query_type, rqsize, zipf, threads, outfile):
  params = {}
  # param_list = ['ds','per','up','range','mfind','rs','n','p','z']
  params['up'] = up
  params['query_type'] = query_type
  params['range'] = 0
  params['trans'] = 0
  params['rs'] =rqsize
  params['z'] = zipf
  params['n'] = size

  str_threads = []
  for th in threads:
    str_threads.append(str(th))

  algs = []
  for alg in ds:
    if alg in ds_list_other:
      algs.append({'ds' : alg, 'per' : ''})
    else:
      for pp in per:
        algs.append({'ds':alg, 'per' : pp})

  # print(graphtitle)
  ymax = 0
  series = {}
  # print(algs)
  # error = {}
  for alg in algs:
    # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and (bench.find('-0rq') == -1 or bench.find('2000000000') != -1):
    #   continue
    # if toString3(alg, 1, bench) not in results:
    #   continue
    params['ds'] = alg['ds']
    params['per'] = alg['per']
    key = alg['ds'] + "_" + alg['per']
    if  alg['per'] == '':
      key = alg['ds']
    series[key] = []
    # error[alg] = []
    for th in threads:
      params['p'] = th
      if toString(params) not in throughput:
        # print(toString(params))
        del series[key]
        # del error[alg]
        break
      series[key].append(throughput[toString(params)])
      # error[alg].append(stddev[toString3(alg, th, bench)])
  
  print(series)

  fig, axs = plt.subplots()
  # fig = plt.figure()
  opacity = 0.8
  rects = {}
  
  for alg in series:
    ymax = max(ymax, max(series[alg]))
    rects[alg] = axs.plot(threads, series[alg],
      alpha=opacity,
      color=colors[alg],
      # hatch=hatch[ds],
      linestyle=linestyles[alg],
      marker=markers[alg],
      linewidth=3.0,
      markersize=14,
      label=names[alg])

  # plt.axvline(x = 128, color = 'black', linestyle='--')

  axs.set_ylim(bottom=-0.02*ymax)
  # plt.xticks(threads, threads)
  axs.set(xlabel='Number of threads', ylabel='Total throughput (Mop/s)')
  legend_x = -0.12
  legend_y = 1.4 
  # if this_file_name == 'Update_heavy_with_RQ_-_100K_Keys':
  #   plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))

  # plt.legend(framealpha=0.0)
  # plt.legend()
  plt.legend(framealpha=0.0, loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.grid()
  ds_type = "arttree"
  if ds[0].find("btree") != -1:
    ds_type = "btree"
  plt.title(ds_type + " scalability")
  plt.savefig(output_folder + '/' + outfile + "_" + ds_type + ".png", bbox_inches='tight')
  plt.close('all')
  
  return


def print_update_graph(throughput, parameters, ds, per, size, ups, query_type, rqsize, zipf, threads, graph_type="", outfile="default"):
  params = {}
  # param_list = ['ds','per','up','range','mfind','rs','n','p','z']
  params['p'] = threads
  params['query_type'] = query_type
  params['range'] = 0
  params['trans'] = 0
  params['rs'] = rqsize
  params['z'] = zipf
  params['n'] = size

  str_ups = []
  for up in ups:
    str_ups.append(str(up))

  algs = []
  for alg in ds:
    if alg in ds_list_other:
      algs.append({'ds' : alg, 'per' : ''})
    else:
      for pp in per:
        algs.append({'ds':alg, 'per' : pp})

  # print(graphtitle)
  ymax = 0
  series = {}
  # error = {}
  for alg in algs:
    # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and (bench.find('-0rq') == -1 or bench.find('2000000000') != -1):
    #   continue
    # if toString3(alg, 1, bench) not in results:
    #   continue
    params['ds'] = alg['ds']
    params['per'] = alg['per']
    key = alg['ds'] + "_" + alg['per']
    if  alg['per'] == '':
      key = alg['ds']
    series[key] = []
    # error[alg] = []
    for up in str_ups:
      params['up'] = up
      if toString(params) not in throughput:
        print(toString(params))
        del series[key]
        # del error[alg]
        break
      series[key].append(throughput[toString(params)])
      # error[alg].append(stddev[toString3(alg, th, bench)])
  
  print(series)

  fig, axs = plt.subplots()
  # fig = plt.figure()
  opacity = 0.8
  rects = {}
  
  for alg in series:
    ymax = max(ymax, max(series[alg]))
    rects[alg] = axs.plot(ups, series[alg],
      alpha=opacity,
      color=colors2[alg],
      # hatch=hatch[ds],
      # linestyle=linestyles[alg],
      # marker=markers[alg],
      marker=markers2[alg],
      linewidth=3.0,
      markersize=14,
      label=names2[alg])

  # plt.axvline(x = 144, color = 'black', linestyle='--')

  axs.set_ylim(bottom=-0.02*ymax)
  # plt.xticks(threads, threads)
  axs.set(xlabel='Update Percentage', ylabel='Total throughput (Mop/s)')
  legend_x = 1
  legend_y = 0.5 
  # if this_file_name == 'Update_heavy_with_RQ_-_100K_Keys':
  #   plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))

  # if graph_type == '-timestamp':
  plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.grid()
  if graph_type == "":
    plt.title("Verlib arttree")
  else:
    plt.title("Verlib hashtable")
  plt.savefig(output_folder + '/' + outfile + ".png", bbox_inches='tight')
  plt.close('all')
  
  return

def print_size_graph(throughput, parameters, ds, per, sizes, up, query_type, rqsize, zipf, threads, outfile):
  params = {}
  # param_list = ['ds','per','up','range','mfind','rs','n','p','z']
  params['p'] = threads
  params['query_type'] = query_type
  params['range'] = 0
  params['trans'] = 0
  params['rs'] = rqsize
  params['z'] = zipf
  params['up'] = up

  str_sizes = []
  for size in sizes:
    str_sizes.append(str(size))

  algs = []
  for alg in ds:
    if alg in ds_list_other:
      algs.append({'ds' : alg, 'per' : ''})
    else:
      for pp in per:
        algs.append({'ds':alg, 'per' : pp})

  # print(graphtitle)
  ymax = 0
  series = {}
  # error = {}
  for alg in algs:
    # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and (bench.find('-0rq') == -1 or bench.find('2000000000') != -1):
    #   continue
    # if toString3(alg, 1, bench) not in results:
    #   continue
    params['ds'] = alg['ds']
    params['per'] = alg['per']
    key = alg['ds'] + "_" + alg['per']
    if  alg['per'] == '':
      key = alg['ds']
    series[key] = []
    # error[alg] = []
    for size in str_sizes:
      params['n'] = size
      if toString(params) not in throughput:
        print(toString(params))
        del series[key]
        # del error[alg]
        break
      series[key].append(throughput[toString(params)])
      # error[alg].append(stddev[toString3(alg, th, bench)])
  
  print(series)

  fig, axs = plt.subplots()
  # fig = plt.figure()
  opacity = 0.8
  rects = {}
  
  for alg in series:
    ymax = max(ymax, max(series[alg]))
    rects[alg] = axs.plot(sizes, series[alg],
      alpha=opacity,
      color=colors2[alg],
      # hatch=hatch[ds],
      # linestyle=linestyles[alg],
      marker=markers2[alg],
      # marker='o',
      linewidth=3.0,
      markersize=14,
      label=names2[alg])

  # plt.axvline(x = 144, color = 'black', linestyle='--')

  axs.set_ylim(bottom=-0.02*ymax)
  axs.set_xscale('log')
  plt.xticks(sizes, ["$10^" + str(int(math.log10(x))) + "$" for x in sizes])
  axs.set(xlabel='Data Structure Size', ylabel='Total throughput (Mop/s)')
  # if this_file_name == 'Update_heavy_with_RQ_-_100K_Keys':
  #   plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))

  legend_x = 1
  legend_y = 0.5 

  # plt.legend(framealpha=0.0)
  plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.grid()
  plt.title("Verlib arttree")
  plt.savefig(output_folder + '/' + outfile + ".png", bbox_inches='tight')

  # legend = plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y), ncol=7, framealpha=0.0)
  # export_legend(legend, output_folder+'/optimization-legend.pdf')

  plt.close('all')
  
  return


def print_zipf_graph(throughput, parameters, ds, per, size, up, query_type, rqsize, zipfs, threads, graph_type="", outfile="default"):
  params = {}
  # param_list = ['ds','per','up','range','mfind','rs','n','p','z']
  params['p'] = threads
  params['query_type'] = query_type
  params['range'] = 0
  params['trans'] = 0
  params['rs'] = rqsize
  params['n'] = size
  params['up'] = up

  str_zipfs = []
  for zipf in zipfs:
    str_zipfs.append(str(zipf))

  algs = []
  for alg in ds:
    if alg in ds_list_other:
      algs.append({'ds' : alg, 'per' : ''})
    else:
      for pp in per:
        algs.append({'ds':alg, 'per' : pp})

  # print(graphtitle)
  ymax = 0
  series = {}
  # error = {}
  for alg in algs:
    # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and (bench.find('-0rq') == -1 or bench.find('2000000000') != -1):
    #   continue
    # if toString3(alg, 1, bench) not in results:
    #   continue
    params['ds'] = alg['ds']
    params['per'] = alg['per']
    key = alg['ds'] + "_" + alg['per']
    if  alg['per'] == '':
      key = alg['ds']
    series[key] = []
    # error[alg] = []
    for zipf in str_zipfs:
      params['z'] = zipf
      if toString(params) not in throughput:
        print(toString(params))
        del series[key]
        # del error[alg]
        break
      series[key].append(throughput[toString(params)])
      # error[alg].append(stddev[toString3(alg, th, bench)])
  
  print(series)

  fig, axs = plt.subplots()
  # fig = plt.figure()
  opacity = 0.8
  rects = {}
  
  for alg in series:
    ymax = max(ymax, max(series[alg]))
    rects[alg] = axs.plot(zipfs, series[alg],
      alpha=opacity,
      color=colors2[alg],
      # hatch=hatch[ds],
      # linestyle=linestyles[alg],
      marker=markers2[alg],
      # marker='o',
      linewidth=3.0,
      markersize=14,
      label=names2[alg])

  # plt.axvline(x = 144, color = 'black', linestyle='--')

  axs.set_ylim(bottom=-0.02*ymax)
  # axs.set_xscale('log')
  # plt.xticks(zipfs, zipfs)
  axs.set(xlabel='Zipfian parameter', ylabel='Total throughput (Mop/s)')
  legend_x = 1
  legend_y = 0.5 
  # if this_file_name == 'Update_heavy_with_RQ_-_100K_Keys':
  #   plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))

  # plt.legend(framealpha=0.0)
  plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.grid()
  plt.title("Verlib arttree")
  plt.savefig(output_folder + '/' + outfile + ".png", bbox_inches='tight')
  plt.close('all')
  
  return


if not os.path.exists(output_folder):
  os.makedirs(output_folder)


def old_code():
  mpl.rcParams.update({'font.size': 25})

  print_update_graph(throughputs, parameters, ['hash_block_cas_versioned'], 
    ["ws", "rs", "tl2s", "ls", "hs", "ns"], 10000000, [0,5,20,50,100],"mfind",16, 0, 127, "-timestamp")
  # print_zipf_graph(throughputs, parameters, ['hash_block_cas_versioned'], 
    # ["ws", "rs", "tl2s", "ls", "hs", "ns"], 10000000, 20,"mfind",16, [0,.5,.75,.9,.95,.99], 127, "-timestamp")

  # this was only run with finds and updates, no snapshotted operations
  # one of the lines didn't generate
  print_scalability_graph(throughputs, parameters, ['btree_lf', 'btree_lck', 'srivastava_abtree_pub'], 
    ['noversion', 'versioned_hs'], 10000000, 5,"find",0, 0.99, [1,64,128,192,256,320,384])
  print_scalability_graph(throughputs, parameters, ['arttree_lf', 'arttree_lck', 'leis_olc_art'], 
    ['noversion', 'versioned_hs'], 10000000, 5,"find",0, 0.99, [1,64,128,192,256,320,384])

  # advantage of shortcutting reduces as number of updates increases
  print_update_graph(throughputs, parameters, ['arttree_lf'], 
    ["noversion", "versioned_hs", "noshortcut_hs", "indirect_hs"], 10000000, [0,5,20,50,100],"mfind",16, 0, 127)
  print_size_graph(throughputs, parameters, ['arttree_lf'], 
    ["noversion", "versioned_hs", "noshortcut_hs", "indirect_hs"], [10000,1000000,1000000,10000000,100000000], 20,"mfind",16, 0, 127)
  print_zipf_graph(throughputs, parameters, ['arttree_lf'], 
    ["noversion", "versioned_hs", "noshortcut_hs", "indirect_hs"], 10000000, 20,"mfind",16, [0,.2,.4,.6,.8,.9,.99], 127)

  # params = {'p': parameters['p'][0], 'z': zipf,}

  # print_table_timestamp_inc(throughputs, parameters, ds, per, n, mix_percents_ts, params)

  print("done")