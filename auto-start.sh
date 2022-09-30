#!/bin/bash
sleep 15 &&
    TP_PATH=$(pwd)
TP_PATH='${HOME}/bin/fan_control'
sudo -c "${TP_PATH}" &
exit
