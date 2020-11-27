#!/bin/bash

set -e

st-flash write build/irusb.bin 0x8000000

