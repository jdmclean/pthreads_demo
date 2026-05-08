#!/bin/bash

# Check command line params
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 directory1 directory2"
    exit 1
fi

dir1="$1"
dir2="$2"

# Verify both paths are actually directories
if [ ! -d "$dir1" ] || [ ! -d "$dir2" ]; then
    echo "Error: Both arguments must be directories."
    exit 1
fi

# Iterate through files in the first directory
find "$dir1" -maxdepth 1 -type f | while read -r file1; do
    filename=$(basename "$file1")
    file2="$dir2/$filename"

    # Check if the same filename exists in directory 2
    if [ -f "$file2" ]; then
        # Calculate sums (using awk to grab just the hash)
        sum1=$(md5sum "$file1" | awk '{ print $1 }')
        sum2=$(md5sum "$file2" | awk '{ print $1 }')

        if [ "$sum1" = "$sum2" ]; then
            echo "[MATCH] $filename"
        else
            echo "[DIFF]  $filename (Hashes do not match)"
        fi
    else
        echo "[MISS]  $filename (Not found in $dir2)"
    fi
done

