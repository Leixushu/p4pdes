#!/bin/bash
set -e

# test one application of various multigrid cycles by looking at residual norm
# reduction in 3D case
#   * timings do not matter (but using --with-debugging=0 is convenient)
#   * run as:
#         $ ./cyclereductions > residuals.txt
#   * generates additional file transcript.txt
#   * python one-liner to extract norm-reduction ratios:
#         import numpy; v = numpy.loadtxt('residuals.txt'); v[1::2] / v[::2]

LEV=6    # 129x129x129 grid; each run takes less than 5 seconds?
#LEV=7    # 257x257x257 grid; each run takes 30 seconds?

TRANSCRIPT=transcript.txt

COMMON="-fsh_dim 3 -da_refine $LEV -snes_type ksponly -ksp_type richardson -ksp_max_it 1 -ksp_norm_type unpreconditioned -ksp_monitor -pc_type mg -mg_levels_${LEV}_ksp_converged_reason"

function runcase() {
  CMD="../fish $COMMON -pc_mg_type $1 -mg_levels_ksp_max_it $2 $3 $4"
  echo $CMD >> $TRANSCRIPT
  rm -f tmp.txt
  /usr/bin/time -f "real %e" $CMD &> tmp.txt
  cat tmp.txt >> $TRANSCRIPT
  grep "KSP Residual norm" tmp.txt | awk 'NF>1{print $NF}'  # extract number only
  rm -f tmp.txt
}

rm -f $TRANSCRIPT
#for extra in "" "-fsh_initial_type random" "-fsh_problem manupoly -fsh_cx 0.01 -fsh_cz 100.0" ; do
for extra in "" "-fsh_initial_type random" ; do
    runcase multiplicative 2 "-pc_mg_cycle_type v" "$extra"
    runcase multiplicative 2 "-pc_mg_cycle_type w" "$extra"
    runcase multiplicative 1 "-pc_mg_cycle_type v -pc_mg_multiplicative_cycles 2" "$extra"
    runcase multiplicative 1 "-pc_mg_cycle_type w -pc_mg_multiplicative_cycles 2" "$extra"
    runcase full 2 "" "$extra"
    runcase kaskade 4 "" "$extra"
done

