#! /bin/bash
pushd build
make
make install
popd

make clean
rm dumper
make dumper

