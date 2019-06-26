#!/bin/bash
killall nns-freeplay-battery-daemon
g++ nns-freeplay-battery-daemon.cpp -o nns-freeplay-battery-daemon
#./nns-freeplay-battery-daemon -debug 2  -i2caddr 0x4d -adcvref 4.5 -adcres 4096 -vbatfilename "test-vbat.log" -vbatstatsfilename "test-vbat-start.log" -vbatlogging 1
#./nns-freeplay-battery-daemon -register16 0x12.0x1,0x08.0x0BA6,0x0B.0x2D

