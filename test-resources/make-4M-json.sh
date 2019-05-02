#!/bin/bash

file=one-eve-msg.txt
STR=`cat $file`
N=4000000
OUT=eve4M.json.gz

rm -f $OUT

yes $STR | head -n $N |gzip -1vc > $OUT
