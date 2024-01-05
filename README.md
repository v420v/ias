
# IAS

A simple aarch64 assembler (WIP).

## build
```sh
$ g++ -o ias main.cc -O3
```

## usage
```sh
$ ./ias main.s > main.o

$ readelf -h main.o
$ objdump -d main.o
```

