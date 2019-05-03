#!/usr/bin/env bash

CODE=0

for f in tests/*
do
    printf " => Executing file: %-25s\n" "$f"

    ./blu "$f" >/dev/null
    if [ 0 -ne $? ]
    then
        CODE=1
    fi
done

if [ 0 -eq $CODE ]
then
    echo ""
    echo " --- All tests passed successfully :) --- ";
    echo ""
else
    echo ""
    echo " --- Not all tests passed successfully :( --- ";
    echo ""
fi

exit $CODE
