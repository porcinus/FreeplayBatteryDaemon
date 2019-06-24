#!/bin/bash
cp nns-freeplay-battery-daemon.service /lib/systemd/system/nns-freeplay-battery-daemon.service
systemctl enable nns-freeplay-battery-daemon.service
systemctl start nns-freeplay-battery-daemon.service
