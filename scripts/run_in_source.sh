#!/bin/bash
# Meson wrapper script for running Python commands in the source directory

cd "@SOURCE_ROOT@"
exec "$@"
