#!/bin/bash

rm /dev/aggnet0
rmmod aggnet

insmod aggnet.ko 
MAJOR=`cat /proc/devices | grep aggnet | awk '{print $1}'`
mknod /dev/aggnet0 c $MAJOR 0
