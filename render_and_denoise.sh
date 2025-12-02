#!/bin/bash

set -e

echo "========================================="
echo "Rendering and Denoising All Scenes"
echo "========================================="
echo ""

# Scenes to render
SCENES=(
    "cornell_spheres"
    "veach_mis"
    "material_showcase"
    "caustics"
    "bunny"
    "dragon"
)

# Clean output directory
echo "Cleaning output directory..."
rm -rf output/*
echo ""

TOTAL_SCENES=${#SCENES[@]}
CURRENT=0

for SCENE in "${SCENES[@]}"; do
    CURRENT=$((CURRENT + 1))
    echo "========================================="
    echo "[$CURRENT/$TOTAL_SCENES] Processing: $SCENE"
    echo "========================================="

    # Determine config file
    if [ "$SCENE" = "dragon" ]; then
        CONFIG="path_tracer/config/model.yaml"
    else
        CONFIG="path_tracer/config/${SCENE}.yaml"
    fi

    echo "Step 1/3: Rendering..."
    echo "Config: $CONFIG"
    START_TIME=$(date +%s)

    ./build/path_tracer/path_tracer -c "$CONFIG"

    END_TIME=$(date +%s)
    RENDER_TIME=$((END_TIME - START_TIME))
    echo "✓ Rendering completed in ${RENDER_TIME}s"
    echo ""

    # Denoise with A-trous wavelet
    echo "Step 2/3: Denoising with A-trous wavelet..."
    ./build/denoising/atrous_wavelet \
        -c denoising/config/atrous_wavelet.yaml \
        --raw "output/${SCENE}/${SCENE}_raw.hdr" \
        --normal "output/${SCENE}/${SCENE}_normal.hdr" \
        --albedo "output/${SCENE}/${SCENE}_albedo.hdr" \
        --sigma-c 0.5 \
        --sigma-n 0.5 \
        --sigma-a 0.1 \
        --iterations 5 \
        -o "output/${SCENE}/atrous"
    echo "✓ A-trous wavelet denoising completed"
    echo ""

    # Denoise with Joint Bilateral
    echo "Step 3/3: Denoising with Joint Bilateral..."
    ./build/denoising/joint_bilateral \
        -c denoising/config/joint_bilateral.yaml \
        --raw "output/${SCENE}/${SCENE}_raw.hdr" \
        --normal "output/${SCENE}/${SCENE}_normal.hdr" \
        --albedo "output/${SCENE}/${SCENE}_albedo.hdr" \
        --sigma-c 0.3 \
        --sigma-n 0.2 \
        --sigma-p 2.0 \
        --sigma-a 0.1 \
        -o "output/${SCENE}/bilateral"
    echo "✓ Joint Bilateral denoising completed"
    echo ""

    echo "✓ Scene $SCENE completed!"
    echo ""
done

echo "========================================="
echo "All scenes processed successfully!"
echo "========================================="

