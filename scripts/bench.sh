#!/usr/bin/env bash

for i in ./benchmarks/*
do
    printf " => Benchmarking blu: "
    ./blu --version
    time ./blu "$i/blu.blu"
    echo ""

    printf " => Benchmarking python: "
    python --version
    time python "$i/python.py"
    echo ""

    printf " => Benchmarking ruby: "
    ruby --version
    time ruby "$i/ruby.rb"
    echo ""
done
