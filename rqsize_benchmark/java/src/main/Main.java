/**
 * Java test harness for throughput experiments on concurrent data structures.
 * Copyright (C) 2012 Trevor Brown
 * Copyright (C) 2019 Elias Papavasileiou
 * Copyright (C) 2020 Yuanhao Wei
 * Contact (me [at] tbrown [dot] pro) with any questions or comments.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package main;

import org.deuce.transform.Exclude;

import adapters.*;
import main.support.*;

import algorithms.vcas.Camera;
import algorithms.kiwi.KiWi;
import pl.edu.put.concurrent.jiffy.Jiffy;

import java.util.function.Predicate;
import java.io.*;
import java.lang.management.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map.Entry;
import java.util.Scanner;
import java.util.TreeMap;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicInteger;


@Exclude
public class Main {

    // some variables for the test harness
    protected final ThreadMXBean bean = ManagementFactory.getThreadMXBean();
    public static final int RAW_NUMBER_OF_PROCESSORS = Runtime.getRuntime().availableProcessors();
    public static final int NUMBER_OF_PROCESSORS = RAW_NUMBER_OF_PROCESSORS == 8 ? 4 : RAW_NUMBER_OF_PROCESSORS; // override for hyperthreading on i7
    public static final boolean PRINT_FREEMEM = false; // note: just a (rather inaccurate) estimate
    public static boolean print_memory_usage = false;
    private long startFreemem = 0;

    public static final ThreadLocal<Long> routeNodesTraversed = new ThreadLocal<Long>();
    public static final AtomicLong totalRouteNodesTraversed = new AtomicLong();

    public static final ThreadLocal<Long> getBaseNodeCalls = new ThreadLocal<Long>();
    public static final AtomicLong totalGetBaseNodeCalls = new AtomicLong();

    // variables for the experiment
    protected String machine;
    protected int nthreads;
    protected int ntrials;
    protected double nseconds;
    protected String filename;
    protected Ratio ratio;
    protected String alg;
    protected SwitchMap switches;
    protected boolean prefill;
    protected Object treeParam;

    // some timing variables
    protected AtomicLong startUserTime = new AtomicLong(0);
    protected AtomicLong startWallTime = new AtomicLong(0);

    public static AtomicInteger chunk = new AtomicInteger();

    public Main(int nthreads, int ntrials, double nseconds, String filename,
            Ratio ratio, String alg, SwitchMap switches, boolean prefill, Object treeParam) {
        this.nthreads = nthreads;
        this.ntrials = ntrials;
        this.nseconds = nseconds;
        this.filename = filename;
        this.ratio = ratio;
        this.alg = alg;
        this.switches = switches;
        this.prefill = prefill;
        this.treeParam = treeParam;
        this.machine = "unnamed";
        // try {
        //     Scanner in = new Scanner(new File("machine"));
        //     this.machine = in.next();
        //     in.close();
        // } catch (Exception ex) {
        //     System.err.println("machine file not found. everything is still okay.");
        //     this.machine = "unnamed";
        // }
    }

    @Exclude
    protected abstract class Generator<K> {
        final Random rng;
        public Generator(final Random rng) { this.rng = rng; }
        public abstract K next();
    }

    @Exclude
    public final class RandomGenerator extends Generator<Integer> {
        final int maxKey;
        final int id, numberOfIds;

        public RandomGenerator(final int id, final int numberOfIds, final Random rng, final int maxKey) {
            super(rng);
            if (maxKey < 0) throw new RuntimeException("maxKey must be > 0");
            this.maxKey = maxKey;
            this.id = id;
            this.numberOfIds = numberOfIds;
        }

        public Integer next() {
            return rng.nextNatural(maxKey)+1;
        }
    }

    @Exclude
    public final class LeftRightChainGenerator extends Generator<Integer> {
        final static int SCALE = 1;
        final int maxKey;
        final int id;
        final int originalChainSize;
        int chainSize;
        final static int LEFT = -1, RIGHT = 1;
        int rand = -1, last = -1, dir = RIGHT, cnt = 0;

        public LeftRightChainGenerator(final int id, final int chainSize, final Random rng, final int maxKey) {
            super(rng);
            if (maxKey < 1) throw new RuntimeException("maxKey must be >= 1 so that each process is guaranteed a key.");
            this.originalChainSize = chainSize;
            this.maxKey = maxKey;
            this.id = id;
        }

        public Integer next() {
            if (cnt == 0) {
                rand = rng.nextNatural();
                chainSize = rand % originalChainSize + 1;
                //System.out.println("chainsize = " + chainSize);
                if (rand <= Integer.MAX_VALUE / 2) {
                    dir = LEFT;
                } else {
                    dir = RIGHT;
                }
            }
            cnt = (cnt + SCALE) % (chainSize*SCALE);
            return (rand + (dir == LEFT ? -cnt : cnt) + maxKey) % maxKey;
        }
    }

    // @Exclude
    // public final class SequentialGenerator extends Generator<Integer> {
    //     int curKey;
    //     long prevTime;
    //     long counter;

    //     public SequentialGenerator(final int threadID) {
    //         super(null);
    //         curKey = 0;
    //         counter = 0;
    //         prevTime = System.currentTimeMillis();
    //     }

    //     public Integer next() {
    //         curKey += 1;
    //         counter++;
    //         if(counter > 1024) {
    //             counter = 0;
    //         }
    //         long curTime = System.currentTimeMillis();
    //         return ((int)(curTime % (long) 50000))<<10 + counter;
    //     }
    // }

    @Exclude
    public final class SequentialGenerator extends Generator<Integer> {
        int curKey;
        long prevTime;
        int counter;
        int curChunk;
        int threadID;
        private static final int CHUNK_SIZE = 1024;

        public SequentialGenerator(final int threadID) {
            super(null);
            curKey = threadID;
            this.threadID = threadID;
            counter = 0;
            curChunk = 0;
            prevTime = System.currentTimeMillis();
        }

        public Integer next() {
            if(counter == 0) {
                curChunk = chunk.getAndIncrement();
            }
            int key = curChunk*CHUNK_SIZE + counter;
            counter = (counter+1) % CHUNK_SIZE;
            return key;
        }
    }

    @Exclude
    public abstract class GeneratorFactory {
        abstract ArrayList<Generator> getGenerators(Experiment ex, java.util.Random experimentRng);
        abstract String getName();
    }

    @Exclude
    public final class RandomGeneratorFactory extends GeneratorFactory {
        ArrayList<Generator> getGenerators(Experiment ex, java.util.Random rng) {
            ArrayList<Generator> arrays = new ArrayList<Generator>(ex.nprocs);
            for (int i=0;i<ex.nprocs;i++) {
                arrays.add(new RandomGenerator(i, ex.nprocs, new Random(rng.nextInt()), ex.maxkey));
            }
            return arrays;
        }
        String getName() { return "overlapRandom"; }
    }

    @Exclude
    public final class SequentialGeneratorFactory extends GeneratorFactory {
        ArrayList<Generator> getGenerators(Experiment ex, java.util.Random rng) {
            ArrayList<Generator> arrays = new ArrayList<Generator>(ex.nprocs);
            for (int i=0;i<ex.nprocs;i++) {
                arrays.add(new SequentialGenerator(i));
            }
            return arrays;
        }
        String getName() { return "overlapSequential"; }
    }

    @Exclude
    final class LeftRightChainGeneratorFactory extends GeneratorFactory {
        private final int chainSize;
        public LeftRightChainGeneratorFactory(int chainSize) { this.chainSize = chainSize; }
        ArrayList<Generator> getGenerators(Experiment ex, java.util.Random rng) {
            ArrayList<Generator> arrays = new ArrayList<Generator>(ex.nprocs);
            for (int i=0;i<ex.nprocs;i++) {
                arrays.add(new LeftRightChainGenerator(i, chainSize, new Random(rng.nextInt()), ex.maxkey));
            }
            return arrays;
        }
        String getName() { return "overlapLeftRightChain"; }
    }

    @Exclude
    public abstract class Worker<K extends Comparable<? super K>> extends Thread {
        public abstract long getOpCount();
        public abstract long getTrueIns();
        public abstract long getFalseIns();
        public abstract long getTrueDel();
        public abstract long getFalseDel();
        public abstract long getTrueFind();
        public abstract long getFalseFind();
        public abstract long getTrueRQ();
        public abstract long getFalseRQ();
        public abstract long getTrueSnap();
        public abstract long getFalseSnap();
        public abstract long getEndTime();
        public abstract long getStartTime();
        public abstract long getMyStartCPUTime();
        public abstract long getMyStartUserTime();
        public abstract long getMyStartWallTime();
        public abstract long getUserTime();
        public abstract long getWallTime();
        public abstract long getCPUTime();
        public abstract long getKeysum();
    }

    // public class CounterIncrementer extends Thread {


    //     public final void run() {
    //         While 
    //         Camera.camera.timestamp = Camera.camera.timestamp+1;
    //     }
    // }

    @Exclude
    public class TimedWorker<K extends Comparable<? super K>> extends Worker<K> {
        public final long WORK_TIME;
        CyclicBarrier start;
        Generator<Integer> gen;
        AbstractAdapter<K> tree;
        long trueDel, falseDel, trueIns, falseIns, trueFind, falseFind, trueRQ, falseRQ, trueSnap, falseSnap;
        long keysum; // sum of new keys inserted by this thread minus keys deleted by this thread
        final Experiment ex;
        Random rng;

        private long id;
        private ThreadMXBean bean;

        public final AtomicLong sharedStartUserTime;
        public final AtomicLong sharedStartWallTime;
        public long myStartCPUTime;
        public long myStartUserTime;
        public long myStartWallTime;
        public long cpuTime;
        public long userTime;
        public long wallTime;
        public ArrayList<Worker> workers3; // ref to containing array [dirty technique :P...]

        final int threadID;

        public TimedWorker(final long WORK_TIME,
                           final Generator gen,
                           final Experiment ex,
                           final java.util.Random rng,
                           final AbstractAdapter<K> tree,
                           final CyclicBarrier start,
                           final AtomicLong sharedStart,
                           final AtomicLong sharedStartWallTime,
                           final ArrayList<Worker> workers,
                           final int threadID) {
            this.WORK_TIME = WORK_TIME;
            this.gen = gen;
            this.ex = ex;
            this.rng = new Random(rng.nextInt());
            this.tree = tree;
            this.start = start;
            this.sharedStartUserTime = sharedStart;
            this.workers3 = workers;
            this.sharedStartWallTime = sharedStartWallTime;
            this.threadID = threadID;
            //System.out.println(threadID);
        }

        @Override
        @SuppressWarnings("empty-statement")
        public final void run() {
            routeNodesTraversed.set((long) 0);
            getBaseNodeCalls.set((long) 0);
            ThreadID.threadID.set(threadID);
            //Camera.rng.set(rng);
            //System.out.println(ThreadID.threadID.get());
            bean = ManagementFactory.getThreadMXBean();
            if (!bean.isCurrentThreadCpuTimeSupported()) {
                System.out.println("THREAD CPU TIME UNSUPPORTED");
                System.exit(-1);
            }
            if (!bean.isThreadCpuTimeEnabled()) {
                System.out.println("THREAD CPU TIME DISABLED");
                System.exit(-1);
            }
            id = java.lang.Thread.currentThread().getId();

            // everyone waits on barrier
            if (start != null) try { start.await(); } catch (Exception e) { e.printStackTrace(); System.exit(-1); }

            int rqSize = (int) switches.get("rqSize");

            // everyone waits until main thread sets experiment state to RUNNING
            while (ex.state == ExperimentState.PENDING);

            // start timing
            myStartUserTime = bean.getThreadUserTime(id);
            myStartCPUTime = bean.getThreadCpuTime(id);
            myStartWallTime = System.nanoTime();
            sharedStartUserTime.compareAndSet(0, myStartUserTime);
            sharedStartWallTime.compareAndSet(0, myStartWallTime);

            // perform operations while experiment's state is running
            while (ex.state == ExperimentState.RUNNING) {
                final double op = rng.nextNatural() / (double) Integer.MAX_VALUE;
                if (op < ratio.ins) {
                    final Integer keyInt = gen.next();
                    final K key = (K) keyInt;
                    if (tree.add(key, rng)) {
                        keysum += (Integer) key;
                        trueIns++;
                    } else {
                        falseIns++;
                        if(switches.get("generator") == Globals.GENERATOR_TYPE_SEQUENTIAL)
                            System.out.println("error: inserts should not be failing in sequential workload");
                    }
                } else if (op < ratio.ins + ratio.del) {
                    final Integer keyInt = gen.next();
                    final K key = (K) keyInt;
                    if (tree.remove(key, rng)) {
                        keysum -= (Integer) key;
                        trueDel++;
                    } else falseDel++;
                } else if (op < ratio.ins + ratio.del + ratio.rq) {
                    final Integer keyInt = rng.nextNatural(ex.maxkey - rqSize + 1)+1;
//                    if(tree instanceof JiffyAdapter) {
//                        if ((((JiffyAdapter)tree.rangeQuery((K) keyInt, (K) ((Integer) (keyInt + rqSize - 1)), 0, null))).size() != 0) trueRQ++;
//                        else falseRQ++;
//                    } else {
                        if (((Object[]) (tree.rangeQuery((K) keyInt, (K) ((Integer) (keyInt + rqSize - 1)), 0, null))).length != 0) trueRQ++;
                        else falseRQ++;
//                    }

                } else {
                    final Integer keyInt = gen.next();
                    // if(ThreadID.threadID.get() == null)
                    //     System.out.println("thread ID null");
                    if (tree.contains((K) keyInt)) trueFind++;
                    else falseFind++;
                }
            }

            // finish timing
            wallTime = System.nanoTime();
            userTime = bean.getThreadUserTime(id);
            cpuTime = bean.getThreadCpuTime(id);

            totalRouteNodesTraversed.getAndAdd(routeNodesTraversed.get());
            totalGetBaseNodeCalls.getAndAdd(getBaseNodeCalls.get());
        }

        public long getOpCount() { return 0; }
        public long getTrueIns() { return trueIns; }
        public long getFalseIns() { return falseIns; }
        public long getTrueDel() { return trueDel; }
        public long getFalseDel() { return falseDel; }
        public long getTrueFind() { return trueFind; }
        public long getFalseFind() { return falseFind; }
        public long getTrueRQ() { return trueRQ; }
        public long getFalseRQ() { return falseRQ; }
        public long getTrueSnap() { return trueSnap; }
        public long getFalseSnap() { return falseSnap; }
        public long getStartTime() { return myStartWallTime; }
        public long getEndTime() { return wallTime; }
        public long getMyStartCPUTime() { return myStartCPUTime; }
        public long getMyStartUserTime() { return myStartUserTime; }
        public long getMyStartWallTime() { return myStartWallTime; }
        public long getUserTime() { return userTime; }
        public long getWallTime() { return wallTime; }
        public long getCPUTime() { return wallTime; }
        public long getKeysum() { return keysum; }
    }

    @Exclude
    public class TimedRQWorker<K extends Comparable<? super K>> extends Worker<K> {
        public final long WORK_TIME;
        CyclicBarrier start;
        Generator<Integer> gen;
        AbstractAdapter<K> tree;
        long trueDel, falseDel, trueIns, falseIns, trueFind, falseFind, trueRQ, falseRQ, trueSnap, falseSnap;
        long keysum; // sum of new keys inserted by this thread minus keys deleted by this thread
        final Experiment ex;
        Random rng;

        private long id;
        private ThreadMXBean bean;

        public final AtomicLong sharedStartUserTime;
        public final AtomicLong sharedStartWallTime;
        public long myStartCPUTime;
        public long myStartUserTime;
        public long myStartWallTime;
        public long cpuTime;
        public long userTime;
        public long wallTime;
        public ArrayList<Worker> workers3; // ref to containing array [dirty technique :P...]

        final int threadID;

        public TimedRQWorker(final long WORK_TIME,
                           final Generator gen,
                           final Experiment ex,
                           final java.util.Random rng,
                           final AbstractAdapter<K> tree,
                           final CyclicBarrier start,
                           final AtomicLong sharedStart,
                           final AtomicLong sharedStartWallTime,
                           final ArrayList<Worker> workers,
                           final int threadID) {
            this.WORK_TIME = WORK_TIME;
            this.gen = gen;
            this.ex = ex;
            this.rng = new Random(rng.nextInt());
            this.tree = tree;
            this.start = start;
            this.sharedStartUserTime = sharedStart;
            this.workers3 = workers;
            this.sharedStartWallTime = sharedStartWallTime;
            this.threadID = threadID;
        }

        @Override
        @SuppressWarnings("empty-statement")
        public final void run() {
            ThreadID.threadID.set(threadID);
            //Camera.rng.set(rng);
            bean = ManagementFactory.getThreadMXBean();
            if (!bean.isCurrentThreadCpuTimeSupported()) {
                System.out.println("THREAD CPU TIME UNSUPPORTED");
                System.exit(-1);
            }
            if (!bean.isThreadCpuTimeEnabled()) {
                System.out.println("THREAD CPU TIME DISABLED");
                System.exit(-1);
            }
            id = java.lang.Thread.currentThread().getId();

            // everyone waits on barrier
            if (start != null) try { start.await(); } catch (Exception e) { e.printStackTrace(); System.exit(-1); }

            int rqSize = (int) switches.get("rqSize");

            // everyone waits until main thread sets experiment state to RUNNING
            while (ex.state == ExperimentState.PENDING);

            // start timing
            myStartUserTime = bean.getThreadUserTime(id);
            myStartCPUTime = bean.getThreadCpuTime(id);
            myStartWallTime = System.nanoTime();
            sharedStartUserTime.compareAndSet(0, myStartUserTime);
            sharedStartWallTime.compareAndSet(0, myStartWallTime);

            // perform operations while experiment's state is running
            while (ex.state == ExperimentState.RUNNING) {
                final Integer keyInt = rng.nextNatural(ex.maxkey - rqSize + 1)+1;
                if(rqSize >= ex.maxkey) {
                    if (((Object[]) (tree.rangeQuery((K) (Integer) 0, (K) (Integer) ex.maxkey, 0, null))).length != 0) trueRQ++;
                    else falseRQ++;
                } else {
                    if(switches.get("queryType") == Globals.QUERY_TYPE_RQ) {
                        if (((Object[]) (tree.rangeQuery((K) keyInt, (K) ((Integer) (keyInt + rqSize - 1)), 0, null))).length != 0) trueRQ++;
                        else falseRQ++;
                    } else if(switches.get("queryType") == Globals.QUERY_TYPE_FINDIF) {
                        tree.findIf((K) keyInt, (K) (Integer) ex.maxkey, (Element<K,K> e) -> ((Integer) e.key % (rqSize) == 0));
                        trueRQ++;
                    } else if(switches.get("queryType") == Globals.QUERY_TYPE_SUCC) {
                        if (((Object[]) (tree.successors((K) keyInt, rqSize))).length != 0) trueRQ++;
                        else falseRQ++;
                    } else if(switches.get("queryType") == Globals.QUERY_TYPE_MULTISEARCH) {
                        K[] keysToQuery = (K[]) new Comparable[rqSize];
                        for(int i = 0; i < rqSize; i++)
                            keysToQuery[i] = (K) (Integer) (rng.nextNatural(ex.maxkey)+1);
                        tree.multiSearch(keysToQuery);
                        trueRQ++;
                        // if (((Object[]) ().length != 0) trueRQ++;
                        // else falseRQ++;
                    } else {
                        System.out.println("Invalid queryType");
                    }
                }
            }

            // finish timing
            wallTime = System.nanoTime();
            userTime = bean.getThreadUserTime(id);
            cpuTime = bean.getThreadCpuTime(id);
        }

        public long getOpCount() { return 0; }
        public long getTrueIns() { return trueIns; }
        public long getFalseIns() { return falseIns; }
        public long getTrueDel() { return trueDel; }
        public long getFalseDel() { return falseDel; }
        public long getTrueFind() { return trueFind; }
        public long getFalseFind() { return falseFind; }
        public long getTrueRQ() { return trueRQ; }
        public long getFalseRQ() { return falseRQ; }
        public long getTrueSnap() { return trueSnap; }
        public long getFalseSnap() { return falseSnap; }
        public long getStartTime() { return myStartWallTime; }
        public long getEndTime() { return wallTime; }
        public long getMyStartCPUTime() { return myStartCPUTime; }
        public long getMyStartUserTime() { return myStartUserTime; }
        public long getMyStartWallTime() { return myStartWallTime; }
        public long getUserTime() { return userTime; }
        public long getWallTime() { return wallTime; }
        public long getCPUTime() { return wallTime; }
        public long getKeysum() { return keysum; }
    }

    @Exclude
    final class BoolHolder { volatile boolean b; }

    @Exclude
    final class FixedNumberOfOpsWorker<K extends Comparable<? super K>> extends Thread {
        final AbstractAdapter<K> tree;
        final CyclicBarrier start, end;
        final long opsToPerform;
        final Random rng;
        final Ratio ratio;
        final int maxkey;
        final BoolHolder done;
        long keysum;

        public FixedNumberOfOpsWorker(
                final AbstractAdapter<K> tree,
                final long opsToPerform,
                final Ratio ratio,
                final int maxkey,
                final Random rng,
                final CyclicBarrier start,
                final CyclicBarrier end,
                final BoolHolder done) {
            this.tree = tree;
            this.opsToPerform = opsToPerform;
            this.ratio = ratio;
            this.maxkey = maxkey;
            this.rng = rng;
            this.start = start;
            this.end = end;
            this.done = done;
        }

        @Override
        public void run() {
            try { start.await(); } catch (Exception ex) { ex.printStackTrace(); System.exit(-1); }

            for (int i=0; i < opsToPerform && !done.b; i++) {
                int key = rng.nextNatural(maxkey)+1;
                if (rng.nextNatural() < ratio.ins * Integer.MAX_VALUE) {
                    if (tree.add((K) (Integer) key, rng)) keysum += key;
                } else {
                    if (tree.remove((K) (Integer) key, rng)) keysum -= key;
                }
            }
            done.b = true;
            try { end.await(); } catch (Exception ex) { ex.printStackTrace(); System.exit(-1); }
        }

        public long getKeysum() {
            return keysum;
        }
    }

    @Exclude
    final class FixedNumberOfKeysWorker<K extends Comparable<? super K>> extends Thread {
        final AbstractAdapter<K> tree;
        final Random rng;
        final int maxkey;
        long keysum;
        long keysAdded;
        final int nthreads;
        final int threadID;

        public FixedNumberOfKeysWorker(
                final AbstractAdapter<K> tree,
                final int maxkey,
                final Random rng,
                final int nthreads,
                final int threadID) {
            this.tree = tree;
            this.maxkey = maxkey;
            this.rng = rng;
            this.nthreads = nthreads;
            this.threadID = threadID;
            
            // System.out.println("ThreadID: " + ThreadID.threadID.get());
        }

        @Override
        public void run() {
            ThreadID.threadID.set(threadID);
            //System.out.println("ThreadID: " + ThreadID.threadID.get());
            if(tree.getClass().getName().equals("adapters.KiwiAdapter")) {
                for (int i=0; i<maxkey/(20*nthreads); i++) {
                    int key = rng.nextNatural(maxkey)+1;
                    if (!tree.contains((K) (Integer) key)) {
                        tree.add((K) (Integer) key, rng);
                        keysum += key;
                        keysAdded++;
                    }
                }
            } else {
                for (int i=0; i<maxkey/(20*nthreads); i++) {
                    int key = rng.nextNatural(maxkey)+1;
                    if (tree.add((K) (Integer) key, rng)) {
                        keysum += key;
                        keysAdded++;
                    }
                }                
            }

        }

        public long getKeysum() {
            return keysum;
        }

        public long getKeysAdded() {
            return keysAdded;
        }
    }

    protected boolean runTrial(
            final PrintStream out,
            final boolean discardResults,
            final boolean shouldMeasureTrees,
            final String prefix,
            final SizeKeysumPair pair,
            final java.util.Random rng,
            final AbstractAdapter<Integer> tree,
            final Experiment ex) {

        // System.out.println("\nMemory Usage Before Benchmark: " + MemoryStats.getSettledUsedMemory() + " bytes");
        // if(tree instanceof VcasBatchChromaticGCAdapter) {
        //     NodeStats cur = ((VcasBatchChromaticGCAdapter)tree).tree.countNodes(false);
        //     NodeStats all = ((VcasBatchChromaticGCAdapter)tree).tree.countNodes(true);
        //     System.out.println("Internal Nodes (before): " + cur.internalNodes);
        //     System.out.println("External Nodes (before): " + cur.externalNodes);
        //     System.out.println("Internal Nodes (before, including version list): " + all.internalNodes);
        //     System.out.println("External Nodes (before, including version list: " + all.externalNodes);
        // }

        totalRouteNodesTraversed.set(new Long(0));
        totalGetBaseNodeCalls.set(new Long(0));
        chunk.set(0);
        //VcasBatchChromaticMapGC.epoch.epochNum = 0;

        if (ex.nprocs > 1 && tree instanceof SequentialStructure) {
            System.err.println("ERROR: sequential data structure used with multiple threads... terminating.");
            System.exit(-1);
        }

        // prepare worker threads to run the trial
        startWallTime = new AtomicLong(0);
        startUserTime = new AtomicLong(0);
        CyclicBarrier start = new CyclicBarrier(ex.nprocs);
        ArrayList<Generator> arrays = ex.factory.getGenerators(ex, rng); // generators supply keys for each thread
        ArrayList<Worker> workers = new ArrayList<Worker>(ex.nprocs);    // these are the threads that perform random operations
        int numOfRQWorkers = (int) switches.get("rqers");
        for (int i=0;i<ex.nprocs-numOfRQWorkers;i++) {
            workers.add(new TimedWorker<Integer>((long) (nseconds*1e9), arrays.get(i), ex, rng, tree, start, startUserTime, startWallTime, workers, i));
        }
        for (int i=ex.nprocs-numOfRQWorkers;i<ex.nprocs;i++) {
            workers.add(new TimedRQWorker<Integer>((long) (nseconds*1e9), arrays.get(i), ex, rng, tree, start, startUserTime, startWallTime, workers, i));
        }

        // perform garbage collection to clean up after the last trial, and record how much GC has happened so far
        System.gc();
        //final long gcTimeStart = totalGarbageCollectionTimeMillis();
        final long gcTimeStart = 0;

        // run the trial
        for (int i=0;i<ex.nprocs;i++) workers.get(i).start();
        ex.state = ExperimentState.RUNNING;
        long localStartTime = System.currentTimeMillis();
        long localEndTime;


        try {
            Thread.sleep((long)(nseconds * 1e3));
        } catch (InterruptedException ex1) {
            ex1.printStackTrace();
            System.exit(-1);
        }

        localEndTime = System.currentTimeMillis();
        ex.state = ExperimentState.STOPPED;

        //System.out.println("\n[*] Number of base nodes: " + ((LockFreeImmTreapCATreeMapSTDRAdapter) tree).getNumberOfBaseNodes());

        // stop all threads and record how much GC has happened so far
        try { for (int i=0;i<ex.nprocs;i++) workers.get(i).join(); }
        catch (InterruptedException e) { e.printStackTrace(); System.exit(-1); }
        final long gcTimeEnd = 0; //totalGarbageCollectionTimeMillis();

        // calculate memory usage
        if(print_memory_usage)
            System.out.println("\nMemory Usage After Benchmark: " + MemoryStats.getSettledUsedMemory() + " bytes");
        
        // if(tree instanceof VcasBatchChromaticGCAdapter) {
        //     NodeStats cur = ((VcasBatchChromaticGCAdapter)tree).tree.countNodes(false);
        //     NodeStats all = ((VcasBatchChromaticGCAdapter)tree).tree.countNodes(true);
        //     System.out.println("Internal Nodes: " + cur.internalNodes);
        //     System.out.println("External Nodes: " + cur.externalNodes);
        //     System.out.println("Internal Nodes (including version list): " + all.internalNodes);
        //     System.out.println("External Nodes (including version list: " + all.externalNodes);
        // }

        //System.out.println("Route Nodes Traversed Per getBaseNode(): " + (1.0*totalRouteNodesTraversed.get()/totalGetBaseNodeCalls.get()));

        // compute key checksum for all threads (including from prefilling) and compare it with the key checksum for the data structure
        long threadsKeysum = pair.keysum;
        for (int i=0;i<ex.nprocs;++i) {
            threadsKeysum += workers.get(i).getKeysum();
        }
        //System.out.println("tree.supportsKeysum() = " + tree.supportsKeysum());
        
        if(!print_memory_usage) {
            if ((long) ex.maxkey > 5000000) {
                System.out.println("Tree too large, skipping keysum validation");
            } else if (tree.supportsKeysum()) {
                // long ntrueins = 0, ntruedel = 0;
                // for (Worker w : workers) {
                //     ntrueins += w.getTrueIns();
                //     ntruedel += w.getTrueDel();
                // }
                // long treesize = tree.size();
                // if(ntrueins - ntruedel != treesize)
                //     System.out.println("==================== ERROR: expected_size=" + (ntrueins - ntruedel) + " does not match tree.size()=" + treesize + " =======================");
                // else
                //     System.out.println("Size checksum validation PASSED (size=" + treesize + ").");
                ThreadID.threadID.set(0);
                long dsKeysum = tree.getKeysum();
                if (dsKeysum != threadsKeysum) {
                    //throw new RuntimeException("threadsKeysum=" + threadsKeysum + " does not match dsKeysum=" + dsKeysum);
                    System.out.println("==================== ERROR: threadsKeysum=" + threadsKeysum + " does not match dsKeysum=" + dsKeysum + " =======================");
                } else {
                    System.out.println("Key checksum validation PASSED (checksum=" + dsKeysum + ", size=" + tree.size() + ").");
                    //tree.debugPrint();
                }
            } else {
                // possibly add an unobtrusive warning that checksums are not computed for this data structure
                System.out.println("NOTICE: the data structure " + tree.getClass().getName() + " does not support key checksum validation.");
            }
        }

        // produce output
        if (!discardResults) {
            long endWallTime = Long.MAX_VALUE;
            for (Thread t : workers) {
                Worker<Integer> w = (Worker<Integer>) t;
                if (w.getEndTime() < endWallTime) endWallTime = w.getEndTime();
            }

            double elapsed = (localEndTime - localStartTime)/1e3;
            out.print(prefix + ",");
            long ntruerq = 0, nfalserq = 0, ntruesnap = 0, nfalsesnap = 0, ntrueins = 0, nfalseins = 0, ntruedel = 0, nfalsedel = 0, ntruefind = 0, nfalsefind = 0;
            for (Worker w : workers) {
                ntrueins += w.getTrueIns();
                nfalseins += w.getFalseIns();
                ntruedel += w.getTrueDel();
                nfalsedel += w.getFalseDel();
                ntruefind += w.getTrueFind();
                nfalsefind += w.getFalseFind();
                ntruerq += w.getTrueRQ();
                nfalserq += w.getFalseRQ();
                ntruesnap += w.getTrueSnap();
                nfalsesnap += w.getFalseSnap();
            }
            int nnodes = 0;
            double averageDepth = 0;
            if (shouldMeasureTrees) {
                if (tree instanceof SetInterface) {
                    double depthSum = ((SetInterface) tree).getSumOfDepths();
                    nnodes = ((SetInterface) tree).sequentialSize();
                    averageDepth = (depthSum / nnodes);
                } // otherwise, we don't know what methods it has!
            }
            long ntrue = ntrueins+ntruedel+ntruefind+ntruerq+ntruesnap, nfalse = nfalseins+nfalsedel+nfalsefind+nfalserq+nfalsesnap;
            long nops = ntrue+nfalse;
            ex.throughput = (long)(nops/(double)elapsed);
            System.out.println("nops: " + nops);
            System.out.println("elapsed: " + elapsed);
            if(numOfRQWorkers == 0)
                System.out.println("throughput: " + ex.throughput/1000000.0 + " Mops/s");
            else {
                double fidThroughput = (ntrueins+ntruedel+ntruefind+nfalseins+nfalsedel+nfalsefind)/(double)elapsed;
                double rqThroughput = (ntruerq+nfalserq)/(double)elapsed;
                System.out.println("find+insert+delete throughput: " + fidThroughput/1000000.0 + " Mops/s");
                System.out.println("RQ throughput: " + rqThroughput/1000000.0 + " Mops/s");
            }
            out.print(ex.nprocs + "," + nops + "," + ex.maxkey + ",");
            out.print(ex.ratio + ",");
            out.print(rng.nextInt() + "," + elapsed + ",");
            out.print(ntrueins + "," + nfalseins + "," + ntruedel + "," + nfalsedel + "," + ntruefind + "," + nfalsefind
                    + "," + ntruerq + "," + nfalserq + "," + ntruesnap + "," + nfalsesnap + "," + switches.get("rqSize") + "," + (-1)
                    + "," + ex.throughput + ",");
            out.print(averageDepth + ",");
            out.print((-1) + "," + (-1) + "," + nnodes + "," + pair.treeSize);
            out.print("," + ex.factory.getName());
            out.print(",unknown");

            // method (whether a data structure is using "put" or "put-if-absent" functionality)
            if (tree.getClass().getName().contains("Put")) {
                out.print(",put");
            } else out.print(",putIfAbsent");

            // experiment
            out.print("," + ex.ratio + "-" + (-1));

            // dummy spots, since metrics don't presently exist, but they do in old spreadsheets with pivotcharts...
            out.print(",,");

            // merged-experiment
            String mergedEx = ex.ratio.ins + "i-" + ex.ratio.del + "d-" + ex.ratio.rq + "rq-" + ex.maxkey;
            if(switches.get("queryType") == Globals.QUERY_TYPE_FINDIF) {
                mergedEx += "-findif";
            } else if(switches.get("queryType") == Globals.QUERY_TYPE_SUCC) {
                mergedEx += "-succ";
            } else if(switches.get("queryType") == Globals.QUERY_TYPE_MULTISEARCH) {
                mergedEx += "-multisearch";
            }
            out.print("," + mergedEx);

            // row for legacy pivot charts
            String name = prefix.substring(0, prefix.indexOf(","));
            out.print("," + machine + "-" + name + "-" + mergedEx +"-"+ex.nprocs);

            // machine
            out.print("," + machine);

            // operations per thread (HARD LIMIT OF 128 FOR THESE VALUES; VALUES FOR HIGHER THREAD COUNTS WILL NOT BE PRINTED)
            for (Worker w : workers) {
                long ops = w.getTrueIns() + w.getFalseIns() + w.getTrueDel() + w.getFalseDel() + w.getTrueFind() +
                        w.getFalseFind() + w.getTrueRQ() + w.getFalseRQ() + w.getTrueSnap() + w.getFalseSnap();
                out.print(","+ops);
            }
            for (int i=workers.size();i<128;i++) out.print(",");

            // compute minimum starting times for all threads
            long minStartUserTime = Long.MAX_VALUE;
            long minStartWallTime = Long.MAX_VALUE;
            long minStartCPUTime = Long.MAX_VALUE;
            for (Worker w : workers) {
                minStartUserTime = Math.min(minStartUserTime, w.getMyStartUserTime());
                minStartWallTime = Math.min(minStartWallTime, w.getMyStartWallTime());
                minStartCPUTime = Math.min(minStartCPUTime, w.getMyStartCPUTime());
            }

            // user start+end times per thread
            for (Worker w : workers) {
                out.print(","+((w.getMyStartUserTime()-minStartUserTime)/1e9)+","+((w.getUserTime()-minStartUserTime)/1e9));
            }
            for (int i=workers.size();i<128;i++) out.print(",,");

            // wall start+end times per thread
            for (Worker w : workers) {
                out.print(","+((w.getMyStartWallTime()-minStartWallTime)/1e9)+","+((w.getWallTime()-minStartWallTime)/1e9));
            }
            for (int i=workers.size();i<128;i++) out.print(",,");

            // CPU start+end times per thread
            for (Worker w : workers) {
                out.print(","+((w.getMyStartCPUTime()-minStartCPUTime)/1e9)+","+((w.getCPUTime()-minStartCPUTime)/1e9));
            }
            for (int i=workers.size();i<128;i++) out.print(",,");

            // elapsed time per thread
            long totalElapsedUserTime = 0, totalElapsedWallTime = 0, totalElapsedCPUTime = 0;
            for (Worker w : workers) {
                totalElapsedUserTime += w.getUserTime()-w.getMyStartUserTime();
                totalElapsedWallTime += w.getWallTime()-w.getMyStartWallTime();
                totalElapsedCPUTime += w.getCPUTime()-w.getMyStartCPUTime();
            }

            // total elapsed times
            out.print(","+totalElapsedUserTime/1e9);
            out.print(","+totalElapsedWallTime/1e9);
            out.print(","+totalElapsedCPUTime/1e9);

            // garbage collection time and desired total elapsed time
            final double gcElapsedTime = (gcTimeEnd-gcTimeStart)/1e3;
            out.print(","+gcElapsedTime);
            out.print(","+nseconds);

            // total time for all threads in trial
            ex.totalThreadTime = (((totalElapsedCPUTime/1e9)+ 0 /*liveThreadsElapsedCPUTime*/)/ex.nprocs+gcElapsedTime);
            out.print(","+ex.totalThreadTime);

            if (PRINT_FREEMEM) {
                System.gc();
                final long freemem = Runtime.getRuntime().freeMemory();
                out.print("," + freemem + "," + (startFreemem - freemem) + "," + (nnodes > 0 ? ((startFreemem - freemem)/nnodes) : 0));
            }

            out.println(); // finished line of output
        }

        //System.out.println("Num Epochs: " + VcasBatchChromaticMapGC.epoch.epochNum);
        return true;
    }

    private long totalGarbageCollectionTimeMillis() {
        final List<GarbageCollectorMXBean> gcbeans = ManagementFactory.getGarbageCollectorMXBeans();
        long result = 0;
        for (GarbageCollectorMXBean gcbean : gcbeans) {
            result += gcbean.getCollectionTime();
        }
        return result;
    }

    @Exclude
    protected static final class Ratio {
        final double del, ins, rq;
        public Ratio(final double ins, final double del, final double rq) {
            if (ins < 0 || del < 0 || rq < 0 || ins+del+rq > 1) throw new RuntimeException("invalid ratio " + ins + "i-" + del + "d-" + rq + "rq");
            this.del = del;
            this.ins = ins;
            this.rq = rq;
        }
        @Override
        public String toString() { return "" + (int)(100*ins) + "i-" + (int)(100*del) + "d-" + (int)(100*rq) + "rq"; }
    }

    protected enum ExperimentState { PENDING, RUNNING, STOPPED }

    @Exclude
    public final class Experiment {
        volatile ExperimentState state = ExperimentState.PENDING;
        double totalThreadTime;
        final String alg, param;
        final int nprocs, maxkey;
        final Ratio ratio;
        final GeneratorFactory factory;
        long throughput; // exists to make access to this convenient so that we can decide whether we have finished warming up

        public Experiment(final String alg, final String param, final int nprocs, final int maxkey, final Ratio ratio, final GeneratorFactory factory) {
            this.alg = alg;
            this.param = param;
            this.nprocs = nprocs;
            this.maxkey = maxkey;
            this.ratio = ratio;
            this.factory = factory;
        }
        @Override
        public String toString() {
            return alg + param + "-" + nprocs + "thr-" + maxkey + "keys-" + ratio + (factory == null ? "null" : factory.getName());
        }
    }

    @Exclude
    public static class SwitchMap {
        private TreeMap<String, Double> backingMap;
        public SwitchMap() { backingMap = new TreeMap<String, Double>(); }
        public int size() { return backingMap.size(); }
        public void put(String key, Double val) { backingMap.put(key, val); }
        public double get(String key) {
            if (!backingMap.containsKey(key)) return 0;
            else return backingMap.get(key);
        }
        public String toString() {
            String s = "";
            boolean first = true;
            for (Entry<String, Double> e : backingMap.entrySet()) {
                s += (first ? "" : " ") + e.getKey() + "=" + e.getValue();
                first = false;
            }
            return s;
        }
    }

    @Exclude
    protected class DualPrintStream {
        private PrintStream stdout, fileout;
        public DualPrintStream(String filename) throws IOException {
            if (filename != null) {
                fileout = new PrintStream(new FileOutputStream(filename));
            }
            stdout = System.out;
        }
        public void print(double x) {
            print(String.valueOf(x));
        }
        public void println(double x) {
            println(String.valueOf(x));
        }
        public void print(String x) {
            stdout.print(x);
            if (fileout != null) fileout.print(x);
        }
        public void println(String x) {
            print(x + "\n");
        }
    }

    public class SizeKeysumPair {
        public final long treeSize;
        public final long keysum;
        public SizeKeysumPair(long treeSize, long keysum) {
            this.treeSize = treeSize;
            this.keysum = keysum;
        }
    }

    public class Pair<A, B> {
        public final A first;
        public final B second;

        public Pair(A first, B second) {
            super();
            this.first = first;
            this.second = second;
        }
    }

    SizeKeysumPair parallelFillToSteadyState(
            final java.util.Random rand,
            final SetInterface<Integer> tree,
            Ratio ratio,
            int maxkey,
            final boolean showProgress,
            final Experiment ex) {

        long keysum = 0;

        // deal with an all-search workload by prefilling 50% insert, 50% delete
        // and normalize other ratios to have 100% updates.
        if (Math.abs(ratio.ins + ratio.del) < 1e-8) ratio = new Ratio(0.5, 0.5, 0);
        else ratio = new Ratio(ratio.ins / (ratio.ins+ratio.del), ratio.del / (ratio.ins+ratio.del), 0);

        final int MAX_REPS = 200;
        final double THRESHOLD_PERCENT = 5; // must be within THRESHOLD_PERCENT percent of expected size to stop
        final int expectedSize = (int)(maxkey * (ratio.ins / (ratio.ins+ratio.del)) + 0.5);
        int treeSize = 0;
        int nreps = 0;
        long startFilling = System.nanoTime();

        int numThreads = Runtime.getRuntime().availableProcessors()-4; 
        if(tree.getClass().getName().equals("adapters.KiwiAdapter"))
            numThreads = KiWi.MAX_THREADS;
        if(numThreads < 1) numThreads = 1;
        System.out.println("Prefilling " + tree.getClass().getName() + " with " + numThreads + " cores...");

        // first, check if this is a data structure incompatible with prefilling
        if (tree instanceof NoPrefillStructure) {
            throw new RuntimeException("Data structure type " + tree.getClass().getName() + " is not compatible with prefilling");
        }

        while (Math.abs(toPercent((double)treeSize / expectedSize) - 100) > THRESHOLD_PERCENT) {
            if (nreps++ > MAX_REPS) {
                System.out.println("WARNING: COULD NOT REACH STEADY STATE AFTER " + nreps + " REPETITIONS.");
                System.out.println("         treesize=" + treeSize + " expected=" + expectedSize + " percentToExpected=" + toPercent((double)treeSize / expectedSize) + " %diff=" + Math.abs(toPercent((double)treeSize / expectedSize) - 100) + " THRESHOLD_PERCENT=" + THRESHOLD_PERCENT);
                System.exit(-1);
            }

            final FixedNumberOfKeysWorker[] workers = new FixedNumberOfKeysWorker[numThreads];
            for (int i=0;i<numThreads;i++) {
                workers[i] = new FixedNumberOfKeysWorker((AbstractAdapter) tree, maxkey, new Random(rand.nextInt()), numThreads, i); // FIX THIS HACKY CAST
            }
            for (int i=0;i<numThreads;i++) workers[i].start();
            try { for (int i=0;i<numThreads;i++) workers[i].join(); }
            catch (InterruptedException e) { e.printStackTrace(); System.exit(-1); }
            //System.out.println(" treesize=" + treeSize + " expected=" + expectedSize + " percentToExpected=" + toPercent((double)treeSize / expectedSize) + " %diff=" + Math.abs(toPercent((double)treeSize / expectedSize) - 100) + " THRESHOLD_PERCENT=" + THRESHOLD_PERCENT);

            for (int i=0;i<numThreads;i++) {
                keysum += workers[i].getKeysum();
                treeSize += workers[i].getKeysAdded();
            }
        }
        if(tree.getClass().getName().equals("adapters.KiwiAdapter") && maxkey <= 20000000) {
            ThreadID.threadID.set(0);
            int actualTreeSize = tree.size();
            System.out.println("Actual tree size: " + actualTreeSize + ", Tree size: " + treeSize);
            treeSize = actualTreeSize;
        }

        long endFilling = System.nanoTime();
        System.out.print("initnodes-" + treeSize + "-");
        System.out.print("in" + toPercent((endFilling-startFilling) / 1e9 / 100) + "s["+nreps+"reps]-");
        return new SizeKeysumPair(treeSize, keysum);
    }

    SizeKeysumPair fillToSteadyState(
            final java.util.Random rand,
            final SetInterface<Integer> tree,
            Ratio ratio,
            int maxkey,
            final boolean showProgress) {

        long keysum = 0;

        // deal with an all-search workload by prefilling 50% insert, 50% delete
        // and normalize other ratios to have 100% updates.
        if (Math.abs(ratio.ins + ratio.del) < 1e-8) ratio = new Ratio(0.5, 0.5, 0);
        else ratio = new Ratio(ratio.ins / (ratio.ins+ratio.del), ratio.del / (ratio.ins+ratio.del), 0);

        final int MAX_REPS = 200;
        final double THRESHOLD_PERCENT = 5; // must be within THRESHOLD_PERCENT percent of expected size to stop
        final int expectedSize = (int)(maxkey * (ratio.ins / (ratio.ins+ratio.del)) + 0.5);
        int treeSize = 0;
        int nreps = 0;
        long startFilling = System.nanoTime();

        int numThreads = 1;    // number of threads to use for prefilling phase

        // first, check if this is a data structure incompatible with prefilling
        if (tree instanceof NoPrefillStructure) {
            throw new RuntimeException("Data structure type " + tree.getClass().getName() + " is not compatible with prefilling");
        }

        while (Math.abs(toPercent((double)treeSize / expectedSize) - 100) > THRESHOLD_PERCENT) {
            if (nreps++ > MAX_REPS) {
                System.out.println("WARNING: COULD NOT REACH STEADY STATE AFTER " + nreps + " REPETITIONS.");
                System.out.println("         treesize=" + treeSize + " expected=" + expectedSize + " percentToExpected=" + toPercent((double)treeSize / expectedSize) + " %diff=" + Math.abs(toPercent((double)treeSize / expectedSize) - 100) + " THRESHOLD_PERCENT=" + THRESHOLD_PERCENT);
                System.exit(-1);
            }

            final FixedNumberOfKeysWorker[] workers = new FixedNumberOfKeysWorker[numThreads];
            for (int i=0;i<numThreads;i++) {
                workers[i] = new FixedNumberOfKeysWorker((AbstractAdapter) tree, maxkey, new Random(rand.nextInt()), 1, i); // FIX THIS HACKY CAST
            }
            for (int i=0;i<numThreads;i++) workers[i].start();
            try { for (int i=0;i<numThreads;i++) workers[i].join(); }
            catch (InterruptedException e) { e.printStackTrace(); System.exit(-1); }
            treeSize = tree.size();
            //System.out.println(" treesize=" + treeSize + " expected=" + expectedSize + " percentToExpected=" + toPercent((double)treeSize / expectedSize) + " %diff=" + Math.abs(toPercent((double)treeSize / expectedSize) - 100) + " THRESHOLD_PERCENT=" + THRESHOLD_PERCENT);

            for (int i=0;i<numThreads;i++) {
                keysum += workers[i].getKeysum();
            }
        }

        long endFilling = System.nanoTime();
        System.out.print("initnodes-" + treeSize + "-");
        System.out.print("in" + toPercent((endFilling-startFilling) / 1e9 / 100) + "s["+nreps+"reps]-");
        return new SizeKeysumPair(treeSize, keysum);
    }

    protected ArrayList<Experiment> getExperiments() {
        final ArrayList<Experiment> exp = new ArrayList<Experiment>();
        GeneratorFactory gen = null;
        if (switches.get("generator") == Globals.GENERATOR_TYPE_DEFAULT) gen = new RandomGeneratorFactory();
        else if (switches.get("generator") == Globals.GENERATOR_TYPE_CHAINS) gen = new LeftRightChainGeneratorFactory(Globals.DEFAULT_CHAIN_SIZE);
        else if (switches.get("generator") == Globals.GENERATOR_TYPE_SEQUENTIAL) gen = new SequentialGeneratorFactory();
        else {
            System.out.println("Critical error with generator selection...");
            System.exit(-1);
        }
        exp.add(new Experiment(alg, treeParam.toString(), nthreads, (int) switches.get("keyRange"), ratio, gen));
        return exp;
    }

    public void run(final PrintStream output) {
        // create output streams
        PrintStream out = output;
        if (out == null) {
            if (filename == null) {
                out = System.out;
            } else {
                try { out = new PrintStream(new File(filename)); }
                catch (Exception e) { e.printStackTrace(); System.exit(-1); }
            }
        }
        DualPrintStream stdout = null;
        try { stdout = new DualPrintStream(filename + "_stdout"); } catch (Exception e) { e.printStackTrace(); System.exit(-1); }

        // print header
        out.print("name"
                + ",trial"
                + ",nthreads"
                + ",threadops"
                + ",maxkey"
                + ",ratio"
                + ",seed"
                + ",time"
                + ",ninstrue"
                + ",ninsfalse"
                + ",ndeltrue"
                + ",ndelfalse"
                + ",nfindtrue"
                + ",nfindfalse"
                + ",nrqtrue"
                + ",nrqfalse"
                + ",nsnaptrue"
                + ",nsnapfalse"
                + ",rqsize"
                + ",snapsize"
                + ",throughput"
                + ",avgnodedepth"
                + ",noise"
                + ",balanceprob"
                + ",nnodes"
                + ",initnnodes"
                + ",factoryname"
                + ",helpedothers"
                + ",method"
                + ",experiment"
                + ",dummy,dummy"
                + ",merged-experiment"
                + ",row"
                + ",machine"
                );
        for (int i=0;i<128;i++) out.print(",thread"+i+"ops");
        for (int i=0;i<128;i++) out.print(",thread"+i+"userstart"+",thread"+i+"userend");
        for (int i=0;i<128;i++) out.print(",thread"+i+"wallstart"+",thread"+i+"wallend");
        for (int i=0;i<128;i++) out.print(",thread"+i+"cpustart"+",thread"+i+"cpuend");
        out.print(",totalelapsedusertime");
        out.print(",totalelapsedwalltime");
        out.print(",totalelapsedcputime");
        out.print(",gctime");
        out.print(",nseconds");
        out.print(",effectivetimeperthread");
        out.print(",restarted");
        out.println();

        // retrieve list of experiments to perform (this is a method because subclasses can implement it differently)
        ArrayList<Experiment> exp = getExperiments();

        // preview experiments, and determine now many runs there will be in total
        for (Experiment ex : exp) {
            System.out.println(ex);
        }
        System.out.println(exp.size() + " experiments in total");
        int numberOfRuns = exp.size() * ntrials;

        // start measuring time for the purpose of progress updates
        final long startTime = System.nanoTime();
        int nCompleted = 0;

        if (PRINT_FREEMEM) {
            System.gc();
            startFreemem = Runtime.getRuntime().freeMemory();
            System.out.println(" free memory: " + startFreemem);
        }
        // perform the experiment
        Random rng = new Random((int) System.nanoTime()); // switches.get("seed"));
        for (Experiment ex : exp) {
            int experimentSeed = rng.nextInt(); //System.nanoTime();
            java.util.Random experimentRng = new java.util.Random(experimentSeed);

            if(ex.nprocs < 32) {
                KiWi.MAX_THREADS = 32;
            } else {
                KiWi.MAX_THREADS = ex.nprocs;
            }

            // find appropriate factory to produce the tree we want for this trial
            // and run the trial
            for (TreeFactory factory : Factories.factories) if (ex.alg.equals(factory.getName())) {
                for (int trial=0;trial<ntrials;++trial) {
                    Camera.camera.timestamp = 0;
                    System.gc();
                    SetInterface<Integer> tree = factory.newTree(ex.param);
                    SizeKeysumPair p = new SizeKeysumPair(0, 0);
                    if (prefill) {
                        if(ex.maxkey > 10000)
                        // if(ex.maxkey > 5000000)
                            p = parallelFillToSteadyState(experimentRng, tree, ex.ratio, ex.maxkey, false, ex);
                        else
                            p = fillToSteadyState(experimentRng, tree, ex.ratio, ex.maxkey, false);
                    }
                    if (!runTrial(out, false, false, factory.getName() + ex.param + "," + trial, p, experimentRng, (AbstractAdapter) tree, ex)) System.exit(-1); // TODO: FIX THE HACKY CAST...
                    progress(stdout, tree, ++nCompleted, trial, factory.getName(), startTime, numberOfRuns, ex);
                }
            }
        }
    }

    void progress(
            DualPrintStream stdout,
            final SetInterface<Integer> tree,
            int z,
            int i,
            String name,
            long startTime,
            int nRuns,
            Experiment ex) {

        double prog = ((int)(1000*(double) z / nRuns)) / 10.0;
        int left = (int)(((System.nanoTime()-startTime)/1e9)*(1-prog/100)/(prog/100));
        int elapsed = (int)((System.nanoTime()-startTime)/1e9+0.5);
        stdout.println(ex + " " + name + (tree.getClass().getName().contains("Put") ? "put" : "") + " trial " + i + " : " + prog + "% done, " + (left/60) + ":" + (left%60) + "s left, elapsed " + elapsed + "s");
    }

    double toPercent(double x) { // keep only 1 decimal point
        return Math.abs((int)(x*1000)/10.0);
    }

    public static void invokeRun(String[] args, final PrintStream output) {
        if (args.length == 1 && args[0].equals("test")) {
            System.out.println("Running Tests...");
            Tests.runTests();
            return;
        }

        if (args.length < 4) {
            System.out.println("Insufficient command-line arguments.");
            System.out.println("Must include: #THREADS #TRIALS SECONDS_PER_TRIAL ALGORITHM");
            System.out.print("Valid algorithms are:");
            for (TreeFactory<Integer> f : Factories.factories) System.out.print(" " + f.getName());
            System.out.println();
            System.out.println("Can also include switches after mandatory arguments:");
            System.out.println("\t-chains   to insert/remove chains of 100 sequential integers, starting at random values");
            System.out.println("\t-s###     to set the random seed (32-bit signed int; default is " + Globals.DEFAULT_SEED + ")");
            System.out.println("\t-prefill  to prefill structures to steady state with random operations");
            System.out.println("\t-file-### to specify an output file to store results in");
            System.out.println("\t-param-## to provide a string parameter that will be passed to the tree factory");
            System.out.println("The following switches determine which operations are run (leftover % becomes search):");
            System.out.println("\t-ins%     to specify what % (0 to 100) of ops should be inserts");
            System.out.println("\t-del%     to specify what % (0 to 100) of ops should be deletes");
            System.out.println("\t-rq%     to specify what % (0 to 100) of ops should be rangequeries");
            System.out.println("\t-rqsize     to specify the rangequery size");
            System.out.println("\t-rqers     to specify the number of threads performing only rangequeries");
            System.out.println("\t-keysM    random keys will be uniformly from range [0,M) (default 1000000)");
            System.exit(-1);
        }
        int nthreads = 0;
        int ntrials = 0;
        double nseconds = 0;
        String filename = null;
        String alg = "";
        boolean prefill = false;
        Object treeParam = "";

        SwitchMap switches = new SwitchMap();
        switches.put("seed", (double) Globals.DEFAULT_SEED);
        switches.put("generator", (double) Globals.GENERATOR_TYPE_DEFAULT);
        switches.put("keyRange", (double) Globals.DEFAULT_KEYRANGE);
        switches.put("rqSize", (double) Globals.DEFAULT_RQ_SIZE);
        switches.put("rqers", (double) 0);
        switches.put("queryType", (double) Globals.QUERY_TYPE_RQ);

        try {
            nthreads = Integer.parseInt(args[0]);
            ntrials = Integer.parseInt(args[1]);
            nseconds = Double.parseDouble(args[2]);
            alg = args[3];
        } catch (Exception ex) {
            System.out.println("NUMBER_OF_THREADS, NUMBER_OF_TRIALS, SECONDS_PER_TRIAL must all be numeric");
            System.exit(-1);
        }
        if (nthreads < 0 /*|| nthreads > THREAD_LIMIT*/) {
            System.out.println("Number of threads n must satisfy 0 <= n"/* <= " + THREAD_LIMIT + " (or else we'll crash MTL)"*/);
            System.exit(-1);
        }
        if (ntrials <= 0) {
            System.out.println("Must run at least 1 trial (recommended to run several and discard the first few)");
            System.exit(-1);
        }
        if (nseconds <= 0) {
            System.out.println("Number of seconds per trial s must satisfy 0 < s (should be at least a second, really)");
            System.exit(-1);
        }
        if (alg.length() == 0 || alg == null) {
            System.out.println("alg cannot be blank or null");
            System.exit(-1);
        }

        int totalOpPercent = 0;
        for (int i=0;i<args.length;i++) {
            if (args[i].startsWith("-")) {
                if (args[i].equals("-chains")) {
                    switches.put("generator", (double) Globals.GENERATOR_TYPE_CHAINS);
                } else if (args[i].equals("-memoryusage")) {
                    print_memory_usage = true;
                } else if (args[i].equals("-seq")) {
                    switches.put("generator", (double) Globals.GENERATOR_TYPE_SEQUENTIAL);
                } else if (args[i].equals("-findif")) {
                    switches.put("queryType", (double) Globals.QUERY_TYPE_FINDIF);
                    System.out.println("queryType: FINDIF");
                } else if (args[i].equals("-succ")) {
                    switches.put("queryType", (double) Globals.QUERY_TYPE_SUCC);
                    System.out.println("queryType: SUCC");
                } else if (args[i].equals("-multisearch")) {
                    switches.put("queryType", (double) Globals.QUERY_TYPE_MULTISEARCH);
                    System.out.println("queryType: MULTISEARCH");
                } else if (args[i].matches("-seed[0-9]+")) {
                    try {
                        switches.put("seed", (double) Integer.parseInt(args[i].substring("-seed".length())));
                    } catch (Exception ex) {
                        System.out.println("Seed must be a 32-bit signed integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-ins[0-9]+(\\.[0-9]+){0,1}")) {
                    try {
                        switches.put("ratio-ins", Double.parseDouble(args[i].substring(4, args[i].length())));
                        totalOpPercent += switches.get("ratio-ins");
                        if (switches.get("ratio-ins") < 0) {
                            System.out.println("The insert percentage must be >= 0");
                            System.exit(-1);
                        }
                    } catch (Exception ex) {
                        System.out.println("The insert percentage must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-del[0-9]+(\\.[0-9]+){0,1}")) {
                    try {
                        switches.put("ratio-del", Double.parseDouble(args[i].substring(4, args[i].length())));
                        totalOpPercent += switches.get("ratio-del");
                        if (switches.get("ratio-del") < 0) {
                            System.out.println("The delete percentage must be >= 0");
                            System.exit(-1);
                        }
                    } catch (Exception ex) {
                        System.out.println("The delete percentage must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-rq[0-9]+(\\.[0-9]+){0,1}")) {
                    try {
                        switches.put("ratio-rq", Double.parseDouble(args[i].substring(3, args[i].length())));
                        totalOpPercent += switches.get("ratio-rq");
                        if (switches.get("ratio-rq") < 0) {
                            System.out.println("The range query percentage must be >= 0");
                            System.exit(-1);
                        }
                    } catch (Exception ex) {
                        System.out.println("The delete percentage must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-keys[0-9]+")) {
                    try {
                        switches.put("keyRange", (double) Integer.parseInt(args[i].substring(5, args[i].length())));
                        if (switches.get("keyRange") < 1) {
                            System.out.println("The key range must be > 0");
                            System.exit(-1);
                        }
                    } catch (Exception ex) {
                        System.out.println("The key range must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-rqsize[0-9]+")) {
                    try {
                        switches.put("rqSize", (double) Integer.parseInt(args[i].substring(7, args[i].length())));
                        // if (switches.get("rqSize") < 1) {
                        //     System.out.println("The rangequery size must be > 0");
                        //     System.exit(-1);
                        // }
                    } catch (Exception ex) {
                        System.out.println("The rangequery size must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].matches("-rqers[0-9]+")) {
                    try {
                        switches.put("rqers", (double) Integer.parseInt(args[i].substring(6, args[i].length())));
                        if (switches.get("rqers") < 0) {
                            System.out.println("The rqers must be >= 0");
                            System.exit(-1);
                        }
                    } catch (Exception ex) {
                        System.out.println("The rqers must be a 32-bit integer.");
                        System.exit(-1);
                    }
                } else if (args[i].startsWith("-param-")) {
                    treeParam = args[i].substring("-param-".length());
                } else if (args[i].startsWith("-file-")) {
                    filename = args[i].substring("-file-".length());
                } else if (args[i].matches("-prefill")) {
                    prefill = true;
                } else {
                    System.out.println("Unrecognized command-line switch: \"" + args[i] + "\"");
                    System.exit(-1);
                }
            }
        }

        if (totalOpPercent > 100) {
            System.out.println("Total percentage over all operations cannot exceed 100");
            System.exit(-1);
        }

        boolean found = false;
        TreeFactory factory = null;
        for (TreeFactory<Integer> f : Factories.factories) {
            String name = f.getName();
            if (name.equals(alg)) {
                found = true;
                factory = f;
                break;
            }
        }
        if (!found) {
            System.out.println("Algorithm \"" + alg + "\" was not recognized.");
            System.out.println("Run this class with no arguments to see a list of valid algorithms.");
            System.exit(-1);
        }

        (new Main(nthreads, ntrials, nseconds, filename,
                new Ratio(switches.get("ratio-ins") / 100., switches.get("ratio-del") / 100., switches.get("ratio-rq") / 100.),// switches.get("ratio-snap") / 100.),
                alg, switches, prefill, treeParam)).run(output);

        //LockFreeImmTreapCATreeMapSTDRAdapter<Integer> tree = (LockFreeImmTreapCATreeMapSTDRAdapter<Integer>) factory.newTree(treeParam);
        /*LockFreePBSTAdapter<Integer> tree = (LockFreePBSTAdapter<Integer>) factory.newTree(treeParam);

        tree.remove(10, null);
        tree.add(10, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.remove(10, null);
        tree.add(10, null);
        tree.add(20, null);
        tree.add(30, null);
        tree.remove(10, null);
        tree.remove(10, null);
        tree.remove(10, null);
        tree.contains(10);
        tree.contains(20);
        tree.contains(30);
        tree.rangeQuery(1, 100, 0, null);
        tree.contains(10);
        tree.rangeQuery(1, 100, 0, null);
        tree.add(10, null);
        tree.add(15, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.remove(15, null);
        tree.remove(15, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.contains(15);
        tree.add(5, null);
        tree.rangeQuery(1, 1000000000, 0, null);
        tree.remove(5, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.add(20, null);
        tree.add(15, null);
        tree.add(25, null);
        tree.remove(5, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.remove(25, null);
        tree.rangeQuery(1, 100, 0, null);
        tree.rangeQuery(1, 10, 0, null);
        tree.rangeQuery(1, 14, 0, null);
        tree.rangeQuery(1, 15, 0, null);
        tree.rangeQuery(1, 16, 0, null);*/


        //tree.add(10, null);
        //tree.remove(10, null);

        /*MyInsertThread t1 = new MyInsertThread(tree);
        MyDeleteThread t2 = new MyDeleteThread(tree);
        t1.start();
        t2.start();
        try { t1.join(); } catch (Exception e) { System.out.println("Insert Exception !"); }
        try { t2.join(); } catch (Exception e) { System.out.println("Delete Exception !"); }
        tree.size();*/
    }

    /*static class MyInsertThread extends Thread {
        private LockFreePBSTAdapter<Integer> tree;

        public MyInsertThread(LockFreePBSTAdapter<Integer> tree) {
            this.tree = tree;
        }

        public void run() {
            tree.add(10, null);
        }
    }

    static class MyDeleteThread extends Thread {
        private LockFreePBSTAdapter<Integer> tree;

        public MyDeleteThread(LockFreePBSTAdapter<Integer> tree) {
            this.tree = tree;
        }

        public void run() {
            for (int i=0; i<3; i++) {
                tree.remove(10, null);
            }
        }
    }*/

    public static void main(String[] args) throws Exception {
        invokeRun(args, null);
    }
}
