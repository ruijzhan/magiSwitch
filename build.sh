#!/bin/bash
gcc main.c -lhiredis -o magiSwitch_server
mkfifo magiFIFO
