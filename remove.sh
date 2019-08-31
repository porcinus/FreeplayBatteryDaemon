#!/bin/bash
systemctl stop nns-freeplay-battery-daemon.service
systemctl disable nns-freeplay-battery-daemon.service
rm /lib/systemd/system/nns-freeplay-battery-daemon.service
