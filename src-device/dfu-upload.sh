#!/bin/bash

set -e

dfu-util -l

dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D build/irusb.bin