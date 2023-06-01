#!/bin/bash

# Job : Utility File to replace js and css files in react builded html
# Check if a file has been passed as parameter
if [ -z "$1" ]; then
  echo "Please specify a filename as parameter"
  exit 1
fi

# Get the filename as parameter
file="$1"

# Modify the strings
# Mac
sed -i '' 's|/assets/index-[0-9a-zA-Z]*.js|index.js|g' $file
sed -i '' 's|/assets/index-[0-9a-zA-Z]*.css|index.css|g' $file

# Linux
# sed -i '' 's|/assets/index-[0-9a-zA-Z]*.js|index.js|g' $file
# sed -i '' 's|/assets/index-[0-9a-zA-Z]*.css|index.css|g' $file