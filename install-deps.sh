#!/bin/bash
# Helper script to install dependencies for the VERSE proxy server

set -e

# Detect OS
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS
    echo "Detected macOS, installing dependencies with Homebrew..."

    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        echo "Homebrew not found. Please install Homebrew first:"
        echo "/bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi

    # Install dependencies
    brew install libwebsockets jansson

elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    # Linux
    echo "Detected Linux, installing dependencies with apt..."

    # Install build essentials if not already installed
    sudo apt-get update
    sudo apt-get install -y build-essential

    # Install dependencies
    sudo apt-get install -y libwebsockets-dev libjansson-dev

else
    echo "Unsupported OS: $OSTYPE"
    echo "Please install libwebsockets and jansson manually."
    exit 1
fi

echo "Dependencies installed successfully!"
echo "You can now build the proxy server with: make" 