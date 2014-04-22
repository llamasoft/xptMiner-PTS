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

best_params=""
best_cpm="-1"

# Benchmark routine.  Many of these will fail.  That's ok, they'll fail quickly.
for b in 24 25 26 27 28 29; do
    for s in 1 2 4 8 16 32; do
        cur_params="-b ${b} -s ${s}"
        log_name="bench_$(echo ${cur_params} | tr -cd '[:alnum:]').log"
        echo "${cur_params}" > ${log_name}
        
        echo "Currently Testing: ${cur_params}"
        timeout ${bench_length} ${miner_exe} ${base_params} ${cur_params} >> ${log_name} 2>&1
        
        # Remove the log if the parameters are invalid, else load the speed
        if [[ -n "$(grep ERROR ${log_name})" ]]; then
            echo "           Result: Failure"
            rm ${log_name}
        else
            echo "           Result: $(grep ^collisions ${log_name} | tail -n 1 | sed 's/;.*//')"
            cur_cpm=$(grep ^collision ${log_name} | awk '{ print $2; }' | tr -d '.')
            if (( cur_cpm > best_cpm )); then
                best_cpm="${cur_cpm}"
                best_params="${cur_params}"
            fi
        fi
        
        echo ""
    done
done


##### PART TWO #####

# Try a few more options with the best parameters only
for v in 1 2; do
    for n in 26 25 24 23 22; do
        cur_params="${best_params} -n ${n} -v ${v}"
        log_name="bench_$(echo ${cur_params} | tr -cd '[:alnum:]').log"
        echo "${cur_params}" > ${log_name}
        
        echo "Currently Testing: ${cur_params}"
        timeout ${bench_length} ${miner_exe} ${base_params} ${cur_params} >> ${log_name} 2>&1
        
        # Remove the log if the parameters are invalid
        # Remove the log if the parameters are invalid, else load the speed
        if [[ -n "$(grep ERROR ${log_name})" ]]; then
            echo "           Result: Failure"
            rm ${log_name}
        else
            echo "           Result: $(grep ^collisions ${log_name} | tail -n 1 | sed 's/;.*//')"
        fi
        
        echo ""
    done
done


##### PART THREE #####

# Amass the results into a file
for bench in `find . -name 'bench_*.log'`; do
    params="$(head -n 1 ${bench})"
    result="$(grep ^collisions ${bench} | tail -n 1 | sed 's/;.*//')"
    echo "${result}; Params: ${params}"
done | sort -n -k 2 > bench_results.log

cat bench_results.log