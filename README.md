# FreeplayBatteryDaemon
This program is design to recover battery voltage and RSOC using LC709203F, MCP3021A or MCP3221A. Then output a test file that can be recover by other software to do stuff.

Note : In MCP3X21A case, RSOC is based on a curve recover after real world testing.


# History

- 0.1a : Initial release, PreAlpha stage, MCP3X21A not fully tested.

# Provided scripts :
- compile.sh : Compile cpp file (run this first), require libpthread-dev.
- install.sh : Install service (need restart).
- remove.sh : Remove service.

Note: Don't miss to edit nns-freeplay-battery-daemon.service set path and others arguments correctly.

