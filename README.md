# FreeplayBatteryDaemon
This program is design to recover battery voltage and RSOC using LC709203F, MCP3021A or MCP3221A. Then output a test file that can be recover by other software to do stuff.

Note : In MCP3X21A case, RSOC is based on a curve recover after real world testing.

# Todo
- Implement register manipulation for LC709203F chip via arguments.

# History
- 0.1a : Initial release, PreAlpha stage, MCP3X21A not fully tested.
- 0.1b : MCP3X21A working, custom I2C registers with 16bits value can be set, Example : '-register16 0x12.0x1,0x08.0x0BA6,0x0B.0x2D'

# Provided scripts :
- compile.sh : Compile cpp file (run this first), require libi2c-dev.
- install.sh : Install service (need restart).
- remove.sh : Remove service.

Note: Don't miss to edit nns-freeplay-battery-daemon.service set path and others arguments correctly.

# Issues
### Overlay ADC settings are good but battery voltage is a little off
Sometime ADC chip report a little wrong value because of input impedance or capacitance.
To correct this, use '-adcoffset', this argument allow a positive or a negative voltage correction directly apply to the computed battery voltage.
