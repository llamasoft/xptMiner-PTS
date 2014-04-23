#!/bin/bash
export LC_ALL='C'

if (( "$#" < "2" )); then
    echo "Usage: $0 \"miner_opts\""
    echo "Example: $0 \"-d 0 -w 128\""
    echo ""
    echo "Using default parameters..."
    echo ""
else
    echo "Using miner parameters: '$*'"
    echo ""
fi

miner_exe=""
base_params="$* -o ypool.net:8080 -u gigawatt.benchmark -p x"
bench_length="20m"

# Find the miner executable
if [[ -x "./xptminer" ]];     then miner_exe="./xptminer";     fi
if [[ -x "./xptMiner.exe" ]]; then miner_exe="./xptMiner.exe"; fi

if [[ ! -x "${miner_exe}" ]]; then
    echo "ERROR: Couldn't find miner executable.  Please make sure it's named 'xptminer' or 'xptMiner.exe'."
    exit 1
fi



##### PART ONE #####

# Benchmark routine.  Many of these will fail.  That's ok, they'll fail quickly.
for b in 24 25 26 27 28 29; do
    for s in 1 2 4 8 16 32; do
        cur_params="-b ${b} -s ${s}"
        log_name="bench_$(echo ${cur_params} | tr -cd '[:alnum:]').log"
        echo "${cur_params}" > ${log_name}
        
        echo "Currently Testing: ${cur_params}"
        timeout ${bench_length} ${miner_exe} ${base_params} ${cur_params} >> ${log_name} 2>&1
        
        # Remove the log if the parameters are invalid, else log the speed
        if [[ -n "$(grep ERROR ${log_name})" ]]; then
            echo "           Result: Failure"
            rm ${log_name}
        else
            result="$(grep ^collisions ${log_name} | tail -n 1 | sed 's/;.*//')"
            
            echo "           Result: ${result}"
            echo "${result}; Params: ${cur_params}" >> bench_results.temp
        fi
        
        echo ""
    done
done


##### PART TWO #####

# Try a few more options with the best parameters only
sort -n -k 2 bench_results.temp | tail -n 3 | sed 's/.*Params: //' | 
while read best_params; do
    for n in 25 24 23 22; do
        cur_params="${best_params} -n ${n}"
        log_name="bench_$(echo ${cur_params} | tr -cd '[:alnum:]').log"
        echo "${cur_params}" > ${log_name}
        
        echo "Currently Testing: ${cur_params}"
        timeout ${bench_length} ${miner_exe} ${base_params} ${cur_params} >> ${log_name} 2>&1
        
        # Remove the log if the parameters are invalid, else log the speed
        if [[ -n "$(grep ERROR ${log_name})" ]]; then
            echo "           Result: Failure"
            rm ${log_name}
        else
            result="$(grep ^collisions ${log_name} | tail -n 1 | sed 's/;.*//')"
            
            echo "           Result: ${result}"
            echo "${result}; Params: ${cur_params}" >> bench_results.temp
        fi
        
        echo ""
    done
done


##### PART THREE #####

# Amass the results into a file
sort -n -k 2 bench_results.temp > bench_results.log

rm bench_results.temp
cat bench_results.log