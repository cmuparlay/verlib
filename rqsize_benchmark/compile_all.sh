
echo "Compiling Java benchmarks..."
cd java/
./compile
cd ..
echo "Finished compiling Java benchmarks"

echo "Compiling C++ benchmarks..."
cd cpp/microbench
make -j rqsize_exp
cd ../../

cd ../build/benchmark
make -j
echo "Finished compiling C++ benchmarks"
