#!/bin/bash

if [ "$1" = "build" ]; then
    if [ -z "$2" ]; then
        target="arangod"
    else
        target="$2"
    fi
    cmake --build ./build-presets/myRelease --parallel 128 --target "$target"
elif [ "$1" = "config" ]; then
    cmake --preset=myRelease
elif [ "$1" = "server" ]; then
    ./build-presets/myRelease/bin/arangod -c ./etc/relative/arangod.conf ./tmp/database-dir
else 
    ./build-presets/myRelease/bin/arangosh -c ./etc/relative/arangosh.conf --server.database test
fi
