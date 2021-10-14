B=bin
D=doc
X=data
I=-I/usr/include/modbus/ -I/usr/include/json-c/ -I/home/aprs/mbusd/src/
SYS: $B/dynoWT1x $B $D $X
	touch SYS

$D:
	mkdir -p $D

$B:
	mkdir -p $B

$X:
	mkdir -p $X

$B/dynoWT1x: dynoWT1x.c test_config.h MillionFractions.h
	cc -g $I dynoWT1x.c -o $B/dynoWT1x -lmodbus  -lmosquitto -ljson-c


