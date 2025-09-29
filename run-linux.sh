#!/bin/bash
# Linux/WSL run script for desktop canvas application

# Set library path for raylib
export LD_LIBRARY_PATH="./raylib-4.5.0_linux_amd64/lib:$LD_LIBRARY_PATH"

# Run the application
./desktop_app