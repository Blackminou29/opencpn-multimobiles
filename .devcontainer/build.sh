#!/bin/bash
echo "=== Compilation Plugin Multi-Mobiles ==="

# Télécharger headers OpenCPN
if [ ! -d "OpenCPN" ]; then
    git clone --depth 1 https://github.com/OpenCPN/OpenCPN.git
fi

# Compiler
g++ -shared -fPIC \
    -I"OpenCPN/include" \
    $(pkg-config --cflags gtk+-3.0) \
    -DBUILDING_PLUGIN \
    -o libmultimobiles_pi.so \
    multimobiles_pi.cpp \
    $(pkg-config --libs gtk+-3.0) 2>&1

if [ -f "libmultimobiles_pi.so" ]; then
    echo "✅ Plugin compilé: libmultimobiles_pi.so"
    ls -la libmultimobiles_pi.so
else
    echo "❌ Échec de compilation"
fi
