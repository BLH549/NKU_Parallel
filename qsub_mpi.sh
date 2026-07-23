#!/bin/sh
#PBS -N qsub_mpi
#PBS -e test.e
#PBS -o test.o
#PBS -l nodes=4:ppn=8

PROJECT_DIR=/home/${USER}/guess
RUN_DIR=/home/${USER}
NP=${MPI_NP:-32}

export OMP_NUM_THREADS=${OMP_NUM_THREADS:-1}
if [ -n "${HASH_BATCH_SIZE}" ]; then
    export HASH_BATCH_SIZE
fi

echo "MPI_NP=${NP}"
echo "OMP_NUM_THREADS=${OMP_NUM_THREADS}"
echo "HASH_BATCH_SIZE=${HASH_BATCH_SIZE:-adaptive}"
echo "Allocated slots:"
sort $PBS_NODEFILE | uniq -c

NODES=$(cat $PBS_NODEFILE | sort | uniq)
NODE_COUNT=$(echo "$NODES" | wc -w)
PPN=$(sort $PBS_NODEFILE | uniq -c | awk 'NR==1 {print $1}')
RANKS_PER_NODE=$((PPN / OMP_NUM_THREADS))
if [ ${RANKS_PER_NODE} -lt 1 ]; then
    RANKS_PER_NODE=1
fi
EFFECTIVE_SLOTS=$((NODE_COUNT * RANKS_PER_NODE))
MPI_NODEFILE=${RUN_DIR}/mpi_nodefile_${PBS_JOBID:-manual}

echo "Nodes=${NODE_COUNT}, PPN=${PPN}, ranks_per_node=${RANKS_PER_NODE}, effective_slots=${EFFECTIVE_SLOTS}"

if [ ${NP} -gt ${EFFECTIVE_SLOTS} ]; then
    echo "Warning: MPI_NP=${NP} is larger than effective_slots=${EFFECTIVE_SLOTS} for OMP_NUM_THREADS=${OMP_NUM_THREADS}." 1>&2
    echo "This may oversubscribe CPU cores." 1>&2
fi

rm -f ${MPI_NODEFILE}
for node in $NODES; do
    i=0
    while [ ${i} -lt ${RANKS_PER_NODE} ]; do
        echo ${node} >> ${MPI_NODEFILE}
        i=$((i + 1))
    done
done

for node in $NODES; do
    scp master_ubss1:${PROJECT_DIR}/main ${node}:${RUN_DIR}/main 1>&2
done

echo "MPI rank host distribution:"
/usr/local/bin/mpiexec -np ${NP} -machinefile ${MPI_NODEFILE} hostname | sort | uniq -c

/usr/local/bin/mpiexec -np ${NP} -machinefile ${MPI_NODEFILE} ${RUN_DIR}/main

for node in $NODES; do
    ssh ${node} "rm -f ${RUN_DIR}/main" 1>&2
done

rm -f ${MPI_NODEFILE}
