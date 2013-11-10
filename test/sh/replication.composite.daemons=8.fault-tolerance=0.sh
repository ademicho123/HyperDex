#!/bin/sh
SPACE="space replication key int A attributes int B, int C subspace B, C create 8 partitions tolerate 0 failures"
exec python test/runner.py --daemons=8 --space="${SPACE}" -- \
     {PATH}/test/replication-stress-test --quiet -n 8 -h {HOST} -p {PORT}
