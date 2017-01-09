#!/bin/sh
echo Building \'phyread-`uname -m`\' ...
gcc fel_lib.c fel.cpp main.cpp -lstdc++ -lusb -o phyread-`uname -m`
echo Done!
