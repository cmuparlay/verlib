
package adapters;

import pl.edu.put.concurrent.MultiversionNavigableMap;
import pl.edu.put.concurrent.jiffy.Jiffy;

import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import main.support.SetInterface;

public class JiffyAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    MultiversionNavigableMap<K, K> tree = new Jiffy<K,K>();

    public boolean contains(K key) {
        return tree.containsKey(key);
    }

    @Override
    public boolean add(K key, Random rng, final int[] metrics) {
        return tree.put(key, key) == null;
//        return tree.put(key, key) == null;
    }

    public boolean add(K key, Random rng) {
        return add(key, rng, null);
    }

    public K get(K key) {
        //return tree.get(key);
        return tree.get(key);
    }

    @Override
    public boolean remove(K key, Random rng, final int[] metrics) {
        return tree.remove(key) != null;
    }

    public boolean remove(K key, Random rng) {
        return remove(key, rng, null);
    }

    @Override
    public Object rangeQuery(K lo, K hi, int rangeSize, Random rng) {
        RangeScanResultHolder rangeScanResultHolder = rangeScanResult.get();
        rangeScanResultHolder.rsResult.clear();
        for(K k : tree.subMap(lo, true, hi, true).keySet()) {
            rangeScanResultHolder.rsResult.push(k);
        }

        Object[] stackArray = rangeScanResultHolder.rsResult.getStackArray();
        int stackSize = rangeScanResultHolder.rsResult.getEffectiveSize();

        // Make a copy of the stack and return it
        Object[] returnArray = new Object[stackSize];
        for (int i = 0; i < stackSize; i++)
            returnArray[i] = stackArray[i];
        return returnArray;
    }

    // @Override
    // public long rangeSum(K lo, K hi, int rangeSize, Random rng) {
    //     return tree.rangeSum(lo, hi);
    // }

    public void addListener(OperationListener l) {}

    public int size() {
        return sequentialSize();
    }

    public KSTNode<K> getRoot() {
        return null;
    }

    public long getSumOfKeys() {
        return 0;
//        return tree.getSumOfKeys();
    }

    public boolean supportsKeysum() {
        return false;
    }

    public long getKeysum() {
        return 0;
//        return tree.getSumOfKeys();
    }

    public int getSumOfDepths() {
        return 0;
    }

    public int sequentialSize() {
        return tree.size();
    }

    private final ThreadLocal<RangeScanResultHolder> rangeScanResult = new ThreadLocal<RangeScanResultHolder>() {
        @Override
        protected RangeScanResultHolder initialValue() {
            return new RangeScanResultHolder();
        }
    };
    private static final class RangeScanResultHolder {
        private Stack rsResult;

        RangeScanResultHolder() {
            rsResult = new Stack();
        }

        private static final class Stack {
            private final int INIT_SIZE = 128;
            private Object[] stackArray;
            private int head = 0;

            Stack() {
                stackArray = new Object[INIT_SIZE];
            }

            final void clear() {
                head = 0;
            }

            final Object[] getStackArray() {
                return stackArray;
            }

            final int getEffectiveSize() {
                return head;
            }

            final void push(final Object x) {
                if (head == stackArray.length) {
                    final Object[] newStackArray = new Object[stackArray.length*4];
                    System.arraycopy(stackArray, 0, newStackArray, 0, head);
                    stackArray = newStackArray;
                }
                stackArray[head] = x;
                ++head;
            }
        }
    }
}
