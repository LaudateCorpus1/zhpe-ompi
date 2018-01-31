#!/bin/bash

ME=`whoami`
OUTFILE=/tmp/${ME}-matmul.out
EXPECTED=`pwd`/expected-output/matmulRMA.expected-out.seed.1
HOSTFILE=`pwd`/hostfile

# verify previous command succeeded or else exit with message 
verify_or_exit ()
{
  code=${1}
  msg=${2}
  if [[ ! ${code} -eq 0 ]]
  then
    echo "${msg}"
    exit 1  
  fi
}


if [[ -f ${OUTFILE} ]]
then
  echo "Removing existing output file: ${OUTFILE}"
  /bin/rm -rf ${OUTFILE}
  verify_or_exit $? "Was not able to remove existing outfile: ${OUTFILE}" 
fi

mpirun --hostfile ${HOSTFILE} --bind-to none `pwd`/matmulRMA 4 2 2 2 > ${OUTFILE}
verify_or_exit $? "Was not able to run \"mpirun --hostfile ${HOSTFILE} --bind-to none `pwd`/matmulRMA 4 2 2 2 > ${OUTFILE}\""

if  [[ ! -f ${OUTFILE} ]] || [[ ! -f ${EXPECTED} ]]
then
   verify_or_exit -1 "TEST FAILED: ${OUTFILE} or ${EXPECTED} file missing"
fi

if [[ `diff ${OUTFILE} ${EXPECTED} | wc -l` -ne 0 ]]
then
   echo "${0}:  TEST FAILED"
   diff ${OUTFILE} ${EXPECTED}
else
   echo "${0}:  TEST PASSED"
fi
