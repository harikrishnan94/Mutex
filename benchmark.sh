#!/usr/bin/env bash

BUILDDIR="./build"
EXECTIME=1
LOCALSECTION=100

while [[ $# -gt 0 ]]; do
	key="$1"

	case $key in
	--builddir)
		BUILDDIR="$2"
		shift # past argument
		shift # past value
		;;

	--exectime)
		EXECTIME="$2"
		shift # past argument
		shift # past value
		;;

	--localsection)
		LOCALSECTION="$2"
		shift # past argument
		shift # past value
		;;

	*) # unknown option
		echo "Unknown Argument: $key" 2>&1
		exit 1
		;;
	esac
done

for critsection in {1,20,50,100,200}; do
	for numthreads in {1,2,4,6,8,12,16,24,32,48,64,96,128,192,256,512,768,1024}; do
		"${BUILDDIR}/bench_parkinglot" --exectime=$EXECTIME --numthreads=$numthreads --critsection=$critsection --localsection=$LOCALSECTION
	done
done
