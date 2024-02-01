
cd rqsize_benchmark

python3 run_rqsize_experiments.py 10000000 5 95 [16,256,4096,65536,1048576] ../build/benchmark/verlib/graphs/Figure9 3 5 50G

cd ..

cd build/benchmark/verlib

python3 run_optimization_experiments.py -u 20 -ls 1000 -s 10000000 -z 0 -mf 16 -p 127 -r 3 -t 5 -f Figure7a

python3 run_size_experiments.py -u 20 -s [10000,1000000,1000000,10000000,100000000] -z 0 -mf 16 -p 127 -r 3 -t 5 -f Figure7b

python3 run_update_experiments.py -u [0,5,20,50,100] -s 10000000 -z 0 -mf 16 -p 127 -r 3 -t 5 -f Figure7c

python3 run_zipf_experiments.py -u 20 -s 10000000 -z [0,.5,.75,.9,.95,.99] -mf 16 -p 127 -r 3 -t 5 -f Figure7d

python3 run_timestamp_experiments.py -u [0,5,20,50,100] -s 10000000 -z 0 -mf 16 -p 127 -r 3 -t 5 -f Figure8

python3 run_scalability_experiments.py -u 5 -s 10000000 -z 0.99 -p [1,64,128,192,256,320,384] -r 3 -t 5 -f Figure10
