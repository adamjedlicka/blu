#!/usr/bin/env bash

for i in ./benchmarks/*
do
    printf " => %s\n" $i

    printf " => Benchmarking blu: "
    ./blu --version
    time ./blu "$i/blu.blu"
    echo ""

    if [[ "$1" == "all" ]]; then
        printf " => Benchmarking python: "
        python --version
        time python "$i/python.py"
        echo ""

        printf " => Benchmarking ruby: "
        ruby --version
        time ruby "$i/ruby.rb"
        echo ""

        printf " => Benchmarking php: "
        php --version | head -1
        time php "$i/php.php"
        echo ""

        printf " => Benchmarking node: "
        node --version
        time node "$i/js.js"
        echo ""

        printf " => Benchmarking lua: "
        lua -v
        time lua "$i/lua.lua"
        echo ""
    fi
done
