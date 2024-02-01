import csv
import matplotlib as mpl
# mpl.use('Agg')
mpl.rcParams['grid.linestyle'] = ":"
mpl.rcParams.update({'font.size': 20})
mpl.rcParams['pdf.fonttype'] = 42
mpl.rcParams['ps.fonttype'] = 42
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
import numpy as np
import os
import statistics as st
import re

columns = ['throughput', 'name', 'nthreads', 'ratio', 'maxkey', 'rqsize', 'time', 'ninstrue', 'ninsfalse', 'ndeltrue', 'ndelfalse', 'nrqtrue', 'nrqfalse', 'merged-experiment']

names = { 
          'LFCA': 'LFCA',
          'Jiffy': 'Jiffy',
          'bst.rq_lockfree' : 'EpochBST',
          'skiplistlock.rq_bundle' : 'BundledSkiplist',
          'citrus.rq_bundle' : 'BundledCitrus',
          'btree_lf_versioned_ls' : 'VerlibBtreeOptTS',
          'btree_lf_versioned_hs' : 'VerlibBtreeHwTS',
          # 'btree_ver_ws' : 'btree_ver_ws',
          'btree_lf_versioned_rs' : 'VerlibBtreeQueryTS',
}
colors = {
          'LFCA': 'C3', 
          'Jiffy': 'C5',
          'bst.rq_lockfree': 'C2',        
          'skiplistlock.rq_bundle' : 'C0',
          'citrus.rq_bundle' : 'C1',
          'btree_lf_versioned_ls' : 'C4',  
          'btree_lf_versioned_hs' : 'C5',
          # 'btree_ver_ws' : 'C6',   
          'btree_lf_versioned_rs' : 'C7',  
}
linestyles = {
              'LFCA': '-', 
              'Jiffy': '-',
              'bst.rq_lockfree': '-',   
              'skiplistlock.rq_bundle' : '-',
              'citrus.rq_bundle' : '-',  
              'btree_lf_versioned_ls' : '-',
              'btree_lf_versioned_hs' : '-',
              # 'btree_ver_ws' : '-',
              'btree_lf_versioned_rs' : '-',
}
markers =    {
              'LFCA': 's',    
              'Jiffy': '|',
              'bst.rq_lockfree': '>',      
              'skiplistlock.rq_bundle' : 'o',
              'citrus.rq_bundle' : 'x',    
              'btree_lf_versioned_ls' : 'D',
              'btree_lf_versioned_hs' : '*',
              # 'btree_ver_ws' : '^',
              'btree_lf_versioned_rs' : '^',
}

algList = ['KIWI', 'SnapTree', 'KSTRQ', 'BPBST64', 'LFCA', 'VcasBatchBSTGC64', 'VcasChromaticBatchBSTGC64']

threadList = [1, 36, 72, 140]
RQsizes = [8, 64, 256, 1024, 8192, 65536]
# threadList = [1, 8, 32, 48, 64]
# legendPrinted = False

def toStringBench(maxkey, ratio, rqsize):
  return str(maxkey) + 'k-' + ratio + '-' + str(rqsize) + 's'

def toRatio(insert, delete, rq):
  return str(insert) + 'i-' + str(delete) + 'd-' + str(rq) + 'rq'

def toString(algname, nthreads, ratio, maxkey, rqsize):
  return algname + '-' + str(nthreads) + 't-' + str(maxkey) + 'k-' + ratio + '-' + str(rqsize) + 's' 

def toString2(algname, nthreads, insert, delete, rq, maxkey, rqsize):
  return toString(algname, nthreads, toRatio(insert, delete, rq), maxkey, rqsize)

def toString3(algname, nthreads, benchmark):
  return algname + '-' + str(nthreads) + 't-' + benchmark

def toString4(algname, threadkey, ratio):
  return algname + '-' + threadkey + '-' + ratio

def toStringRQ(algname, nthreads, maxkey, rqsize):
  return "RQ-" + algname + '-' + str(nthreads) + 't-' + str(maxkey) + 'k-' + str(rqsize) + 's'

def toStringRQ3(algname, rqsize, benchmark, optype):
  return "RQ-" + algname + '-' + benchmark + '-' + str(rqsize) + 's' + '-' + optype

def toStringRQcomplex3(algname, benchmark, querytype, optype):
  if(querytype == 'range'):
    return toStringRQ3(algname, rqsize, benchmark, optype)
  return "RQ-" + algname + '-' + benchmark + '-' + querytype + '-' + optype

def div1Mlist(numlist):
  newList = []
  for num in numlist:
    newList.append(div1M(num))
  return newList

def div1M(num):
  return round(num/1000000.0, 3)

def avg(numlist):
  # if len(numlist) == 1:
  #   return numlist[0]
  total = 0.0
  length = 0
  for num in numlist:
    length=length+1
    total += float(num)
  if length > 0:
    return 1.0*total/length
  else:
    return -1;

param_list = ['ds','per','up','range','rs', 'trans', 'n','p','rq_threads', 'z']

def toParam(string):
  numbers = re.sub('[^0-9.]', ' ', string).split()
  ds = string.split(',')[0]
  param = {'ds' : ds}
  p = param_list[2:]
  for i in range(len(p)):
    if '.' in numbers[i]:
      param[p[i]] = float(numbers[i])
    else:
      param[p[i]] = int(numbers[i])
  param['rs'] = 2*param['rs']
  return param

def readVerlibResultsFile(filename, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs):
  throughputs_raw = {}
  param = {}
  for line in open(filename, "r"):
    if "retry percent" in line:
      key = toStringRQ(param['ds'], param['p'], param['n']*2, param['rs']) + "-retry-percent"
      val = float(line.split(' ')[-1])
      if key not in throughputs_raw:
        throughputs_raw[key] = [val]
      else:
        throughputs_raw[key].append(val)
    if not line.startswith('./'):
      continue
    string = line[2:].replace('../', '').replace('build/benchmark/verlib/', '')
    param = toParam(string)
    if param['ds'] not in algs:
      algs.append(param['ds'])
    key = toStringRQ(param['ds'], param['p'], param['n']*2, param['rs'])
    numbers = re.sub('[^0-9.]', ' ', string).split()
    if key+'-updates' not in throughputs_raw:
      throughputs_raw[key+'-updates'] = []
    if key+'-rqs' not in throughputs_raw:
      throughputs_raw[key+'-rqs'] = []
    throughputs_raw[key+'-rqs'].append(float(numbers[-2]))
    throughputs_raw[key+'-updates'].append(float(numbers[-1]))
  for key in throughputs_raw:
    throughput[key] = sum(throughputs_raw[key])/len(throughputs_raw[key])
    stddev[key] = st.pstdev(throughputs_raw[key])

def readCppResultsFile(filename, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs):
  throughputRaw = {}
  if len(threads) == 0:
    threads.append("")
  if len(ratios) == 0:
    ratios.append("")
  if len(maxkeys) == 0:
    maxkeys.append("")
  alg = ""
  rqsize = ""

  # read csv into throughputRaw
  file = open(filename, 'r')
  for line in file.readlines():
    if line.find('vcasbst') != -1:
      alg = 'vcasbst'
    elif line.find('bst.rq_unsafe') != -1:
      alg = 'bst.rq_unsafe'
    elif line.find('bst.rq_lockfree') != -1:
      alg = 'bst.rq_lockfree'
    elif line.find('skiplistlock.rq_bundle') != -1:
      alg = 'skiplistlock.rq_bundle'
    elif line.find('citrus.rq_bundle') != -1:
      alg = 'citrus.rq_bundle'
    elif line.find('RQSIZE') != -1:
      rqsize = int(line.split("=")[1])
      if rqsize not in rqsizes:
        rqsizes.append(rqsize)
    elif line.find('update throughput') != -1:
      if alg not in algs:
        algs.append(alg)
      key = toStringRQ(alg, threads[0], maxkeys[0], rqsize)+'-updates'
      # print(key)
      if key not in throughputRaw:
        throughputRaw[key] = []
      throughputRaw[key].append(int(line.split(' ')[-1]))
    elif line.find('rq throughput') != -1:
      if alg not in algs:
        algs.append(alg)
      key = toStringRQ(alg, threads[0], maxkeys[0], rqsize)+'-rqs'
      # print(key)
      if key not in throughputRaw:
        throughputRaw[key] = []
      throughputRaw[key].append(int(line.split(' ')[-1]))

  # print(throughputRaw)
  # Average througputRaw into throughput

  for key in throughputRaw:
    results = throughputRaw[key]
    # print(results)
    for i in range(len(results)):
      results[i] = results[i]/1000000.0
    avgResult = avg(results)
    throughput[key] = avgResult
    stddev[key] = st.pstdev(results)
    # print(avgResult)

def readJavaResultsFile(filename, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs):
  columnIndex = {}
  throughputRaw = {}
  times = {}
  # throughput = {}
  # stddev = {}

  # read csv into throughputRaw
  with open(filename, newline='') as csvfile:
    csvreader = csv.reader(csvfile, delimiter=',', quotechar='|')
    for row in csvreader:
      if not bool(columnIndex): # columnIndex dictionary is empty
        for col in columns:
          columnIndex[col] = row.index(col)
      if row[columnIndex[columns[0]]] == columns[0]:  # row contains column titles
        continue
      values = {}
      for col in columns:
        values[col] = row[columnIndex[col]]
      numUpdates = int(values['ninstrue']) + int(values['ninsfalse']) + int(values['ndeltrue']) + int(values['ndelfalse']) 
      numRQs = int(values['nrqtrue']) + int(values['nrqfalse'])
      if values['ratio'] == '50i-50d-0rq' and numRQs > 0:
        key = toStringRQ(values['name'], values['nthreads'], values['maxkey'], int(float(values['rqsize'])))
        if int(values['nthreads']) not in threads:
          threads.append(int(values['nthreads']))
        if int(values['maxkey']) not in maxkeys:
          maxkeys.append(int(values['maxkey']))
        if int(float(values['rqsize'])) not in rqsizes:
          rqsizes.append(int(float(values['rqsize'])))
        if values['name'] not in algs:
          algs.append(values['name'])
        time = float(values['time'])
        if values['merged-experiment'].find('findif') != -1:
          key += '-findif'
        elif values['merged-experiment'].find('succ') != -1:
          key += '-succ'
        elif values['merged-experiment'].find('multisearch-nonatomic') != -1:
          key += '-multisearch-nonatomic'
        elif values['merged-experiment'].find('multisearch') != -1:
          key += '-multisearch'
        if (key+'-updates') not in throughputRaw:
          throughputRaw[key+'-updates'] = []
          times[key+'-updates'] = []
        if (key+'-rqs') not in throughputRaw:
          throughputRaw[key+'-rqs'] = []
          times[key+'-rqs'] = []
        throughputRaw[key+'-updates'].append(numUpdates/time)
        throughputRaw[key+'-rqs'].append(numRQs/time)
        times[key+'-updates'].append(time)
        times[key+'-rqs'].append(time)
      else:
        key = toString(values['name'], values['nthreads'], values['ratio'], values['maxkey'], int(float(values['rqsize'])))
        if int(values['nthreads']) not in threads:
          threads.append(int(values['nthreads']))
        if int(values['maxkey']) not in maxkeys:
          maxkeys.append(int(values['maxkey']))
        if values['ratio'] not in ratios:
          ratios.append(values['ratio'])
        if int(float(values['rqsize'])) not in rqsizes:
          rqsizes.append(int(float(values['rqsize'])))
        if values['name'] not in algs:
          algs.append(values['name'])
        if key not in throughputRaw:
          throughputRaw[key] = []
          times[key] = []
        throughputRaw[key].append(float(values['throughput']))
        times[key].append(float(values['time']))

  for key in throughputRaw:
    resultsAll = throughputRaw[key]
    warmupRuns = int(len(resultsAll)/2)
    # print(warmupRuns)
    results = resultsAll[warmupRuns:]
    for i in range(len(results)):
      results[i] = results[i]/1000000.0
    if avg(results) == 0:
      continue
    avgResult = avg(results)
    throughput[key] = avgResult
    stddev[key] = st.pstdev(results)
    # print(avgResult)

  # return throughput, stddev


def plot_rqsize_graphs(outputFile, graphtitle, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs):
  threads.sort()
  rqsizes.sort()
  maxkeys.sort()

  bench = str(threads[0]) + 't-' + str(maxkeys[0]) + 'k'

  ax = {}
  fig, (ax['rqs'], ax['updates']) = plt.subplots(1, 2, figsize=(15,5))

  for opType in ('rqs', 'updates'):
    series = {}
    error = {}
    ymax = 0
    for alg in algs:
      # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and bench.find('-0rq') == -1:
      #   continue
      series[alg] = []
      error[alg] = []
      for size in rqsizes:
        if toStringRQ3(alg, size, bench, opType) not in throughput:
          print(toStringRQ3(alg, size, bench, opType))
          del series[alg]
          del error[alg]
          break
        if opType == 'rqs':
          series[alg].append(throughput[toStringRQ3(alg, size, bench, opType)]*int(size))
          error[alg].append(stddev[toStringRQ3(alg, size, bench, opType)]*int(size))
        else:
          series[alg].append(throughput[toStringRQ3(alg, size, bench, opType)])
          error[alg].append(stddev[toStringRQ3(alg, size, bench, opType)])

    # create plot

    print(series)

    opacity = 0.8
    rects = {}
     
    for alg in series:
      if alg not in names:
        continue
      if max(series[alg]) > ymax:
        ymax = max(series[alg])
      # print(alg)
      rects[alg] = ax[opType].plot([x/2 for x in rqsizes], series[alg],
        alpha=opacity,
        color=colors[alg],
        #hatch=hatch[ds],
        linestyle=linestyles[alg],
        marker=markers[alg],
        markersize=8,
        label=names[alg])
    ax[opType].set_xscale('log', base=2)
    # rqlabels=(8, 64, 258, '1K', '8K', '64K')
    # plt.xticks(rqsizes, rqlabels)
    # if opType == 'rqs':
    #   ax[opType].set_yscale('log')
    # else:
    #   ax[opType].set_ylim(bottom=-0.02*ymax)
    ax[opType].set_xticks(rqsizes)
    ax[opType].set(xlabel='Range query size')
    if opType == 'rqs':
      ax[opType].set_ylabel('RQ throughput (keys/s)')
    else:
      ax[opType].set_ylabel('Update throughput (Mop/s)')
    ax[opType].grid()
    # ax[opType].title.set_text(graphtitle+" "+opType.replace('rqs', 'RQ').replace('updates', 'update') + ' throughput')

  legend_x = 1
  legend_y = 0.5 
  plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.savefig(outputFile, bbox_inches='tight')
  plt.close('all')

def plot_retry_percent_graphs(outputFile, graphtitle, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs):
  threads.sort()
  rqsizes.sort()
  maxkeys.sort()

  bench = str(threads[0]) + 't-' + str(maxkeys[0]) + 'k'

  fig, ax = plt.subplots()

  series = {}
  error = {}
  ymax = 0

  opType = 'retry-percent'

  for alg in algs:
    # if (alg == 'BatchBST64' or alg == 'ChromaticBatchBST64') and bench.find('-0rq') == -1:
    #   continue
    series[alg] = []
    error[alg] = []
    for size in rqsizes:
      if toStringRQ3(alg, size, bench, opType) not in throughput:
        del series[alg]
        del error[alg]
        break
      series[alg].append(throughput[toStringRQ3(alg, size, bench, opType)])
      error[alg].append(stddev[toStringRQ3(alg, size, bench, opType)])

  # create plot

  print(series)

  opacity = 0.8
  rects = {}
   
  for alg in series:
    if max(series[alg]) > ymax:
      ymax = max(series[alg])
    # print(alg)
    rects[alg] = ax.plot([x/2 for x in rqsizes], series[alg],
      alpha=opacity,
      color=colors[alg],
      #hatch=hatch[ds],
      linestyle=linestyles[alg],
      marker=markers[alg],
      markersize=8,
      label=names[alg])
  ax.set_xscale('log', base=2)
  # rqlabels=(8, 64, 258, '1K', '8K', '64K')
  # plt.xticks(rqsizes, rqlabels)
  # if opType == 'rqs':
  #   ax.set_yscale('log')
  # else:
  #   ax.set_ylim(bottom=-0.02*ymax)
  ax.set_xticks(rqsizes)
  ax.set(xlabel='Range query size', ylabel='Retry percent')
  ax.grid()
  # ax.title.set_text(graphtitle+" retry percent")

  legend_x = 1
  legend_y = 0.5 
  plt.legend(loc='center left', bbox_to_anchor=(legend_x, legend_y))
  plt.savefig(outputFile.replace('.png', '-retry-percent.png').replace('.pdf', '-retry-percent.pdf'), bbox_inches='tight')
  plt.close('all')

def plot_combined_rqsize_graphs(inputFileCpp, inputFileJava, inputFileVerlib, outputFile, graphtitle):
  throughput = {}
  stddev = {}
  threads = []
  ratios = []
  maxkeys = []
  rqsizes = []
  algs = []

  readJavaResultsFile(inputFileJava, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs)
  readCppResultsFile(inputFileCpp, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs)
  readVerlibResultsFile(inputFileVerlib, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs)
  print(throughput)
  print(algs)
  print(rqsizes)
  print(threads)
  plot_rqsize_graphs(outputFile, graphtitle, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs)
  # plot_retry_percent_graphs(outputFile, graphtitle, throughput, stddev, threads, ratios, maxkeys, rqsizes, algs)
