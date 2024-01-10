
# IAS

A simple aarch64 assembler (WIP).

## build
```sh
$ g++ -o ias main.cc -O3
```

## usage

`main.s`
```asm
mov x0, #34
mov w8, #93
svc #0
```

```sh
$ ./ias main.s > main.o

$ readelf -h main.o
$ objdump -d main.o
```

## A64 Instruction encoding
https://developer.arm.com/documentation/ddi0602/2023-12 Arm A-profile A64 Instruction Set Architecture


