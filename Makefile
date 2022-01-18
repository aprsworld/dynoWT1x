I=-I/usr/include/modbus/ -I/usr/include/json-c/ -I/home/aprs/mbusd/src/
W=-Wreturn-type -Wunused-variable -Wunused-function

dynoWT1x: dynoWT1x.c dynoWT1x_sweep_configurations.h MillionFractions.h
	cc -g $I $W dynoWT1x.c -o dynoWT1x -lmodbus  -lmosquitto -ljson-c


