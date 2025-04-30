#!/bin/bash

echo "Deleting old built files..."
rm -rf ./build/ > /dev/null 2>&1

echo "cmake creating compile files..."
mkdir ./build/
cd ./build/
cmake -DPICO_BOARD=pico2_w -DWIFI_SSID="MyHome" -DWIFI_PASSWORD="abcd.5678" ..

echo "making project ..."
make > ./make.out 2>&1
cd ..

echo "done ..."

