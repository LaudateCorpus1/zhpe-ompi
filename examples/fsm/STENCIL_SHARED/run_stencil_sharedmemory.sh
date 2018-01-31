#!/bin/bash

ME=`whoami`
OUTFILE=/tmp/${ME}-stencil.out
EXPECTED=`pwd`/expected-output/stencil.expected-output
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

mpirun --hostfile ${HOSTFILE} --bind-to none `pwd`/main 10 20 2 2 | grep "Truncated Final Result" > ${OUTFILE}
verify_or_exit $? "Was not able to run: mpirun --hostfile ${HOSTFILE} --bind-to none `pwd`/main 10 20 2 2 | grep \"Truncated Final Result\" > ${OUTFILE}"


if [[ `diff ${OUTFILE} ${EXPECTED} | wc -l` -ne 0 ]]
then
   echo "${0}:  TEST FAILED"
   diff ${OUTFILE} ${EXPECTED}
else
   echo "${0}:  TEST PASSED"
fi
