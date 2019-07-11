# FreeplayBatteryDaemon
This program is design to recover battery voltage and RSOC using LC709203F, MCP3021A or MCP3221A. Then output a test file that can be recover by other software to do stuff.

Note : In MCP3X21A case, RSOC is based on a curve recover after real world testing.

# History
- 0.1a : Initial release, PreAlpha stage, MCP3X21A not fully tested.
- 0.1b : MCP3X21A working, custom I2C registers with 16bits value can be set, Example : '-register16 0x12.0x1,0x08.0x0BA6,0x0B.0x2D'.
- 0.1c : RSOC value can be extend, Example : '-rsocextend 90,0,100,20', value of 90 will be 100 and 0 will be 20. This argument is used to try to correct LC709203F HG−CVR algorithm.

# Provided scripts :
- compile.sh : Compile cpp file (run this first), require libi2c-dev.
- install.sh : Install service (need restart).
- remove.sh : Remove service.

Note: Don't miss to edit nns-freeplay-battery-daemon.service set path and others arguments correctly.

# Todo
Add support for Maxim MAX17048G and alerr handling.

# Issues
### Overlay ADC settings are good but battery voltage is a little off
Sometime ADC chip report a little wrong value because of input impedance or capacitance.
To correct this, use '-adcoffset', this argument allow a positive or a negative voltage correction directly apply to the computed battery voltage.

### LC709203F report wrong RSOC
Because of HG−CVR algorithm, if there is some impedance on the line, the battery considered as having a huge wear. Fully charged battery is okay but battery drop to 0% at 3.50v for example.
To correct this, use '-rsocextend', please note that this way can limit the lower or higher value or both.
