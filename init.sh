#!/bin/bash

git submodule init && git submodule update 

if [ $? -ne 0 ]; then
  echo "git submodule error"
  exit 1
fi


sudo apt-get install flex bison libssl-dev xtensor-dev libxtensor-blas-dev 

if [ $? -ne 0 ]; then
  echo "install dependency error"
  exit 1
fi

./release.sh config


if [ $? -ne 0 ]; then
  echo "cmake config error"
  exit 1
fi

./release.sh build all

