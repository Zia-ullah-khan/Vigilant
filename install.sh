#!/bin/bash
set -e

if [ "$EUID" -ne 0 ]; then 
  echo -e "\033[0;31mPlease run with sudo:\033[0m sudo ./install.sh"
  exit 1
fi

CYAN='\033[0;36m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

clear
echo -e "${CYAN}"
echo "    ██╗   ██╗██╗ ██████╗ ██╗██╗      █████╗ ███╗   ██╗████████╗"
echo "    ██║   ██║██║██╔════╝ ██║██║     ██╔══██╗████╗  ██║╚══██╔══╝"
echo "    ██║   ██║██║██║  ███╗██║██║     ███████║██╔██╗ ██║   ██║   "
echo "    ╚██╗ ██╔╝██║██║   ██║██║██║     ██╔══██║██║╚██╗██║   ██║   "
echo "     ╚████╔╝ ██║╚██████╔╝██║███████╗██║  ██║██║ ╚████║   ██║   "
echo "      ╚═══╝  ╚═╝ ╚═════╝ ╚═╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝   ╚═╝   "
echo -e "             ${BLUE}>> Ultra-Light Service Manager <<${NC}\n"

OS=$(uname -s)
echo -e "${BLUE}[1/6]${NC} Detected OS: ${CYAN}${OS}${NC}"

BINARY="vigilant-linux-x64"
URL="https://github.com/Zia-ullah-khan/Vigilant/releases/latest/download/${BINARY}"

echo -e "${BLUE}[2/6]${NC} Attempting to download pre-built binary..."
if curl -sLf -o /tmp/vigilant "$URL"; then
    echo -e "${GREEN}[DONE]${NC} Binary downloaded successfully."
else
    echo -e "${BLUE}[INFO]${NC} Pre-built binary not found. Building from source..."
    
    echo -e "${BLUE}[2/6]${NC} Installing dependencies..."
    apt-get update -y && apt-get install -y cmake build-essential libssl-dev git

    BUILD_DIR="/tmp/vigilant_build"
    rm -rf "$BUILD_DIR"
    git clone --depth 1 https://github.com/Zia-ullah-khan/Vigilant.git "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo -e "${BLUE}[3/6]${NC} Compiling Vigilant + Unit Tests..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release -j$(nproc)

    echo -e "${BLUE}[4/6]${NC} Running Unit Tests..."
    if ./build/vigilant_tests "~[integration]"; then
        echo -e "${GREEN}[PASS]${NC} All tests passed."
        cp build/vigilant /tmp/vigilant
        cd /tmp
        rm -rf "$BUILD_DIR"
    else
        echo -e "${RED}[FAIL]${NC} Unit tests failed! Installation aborted."
        rm -rf "$BUILD_DIR"
        exit 1
    fi
fi

echo -e "${BLUE}[5/6]${NC} Moving binary to /usr/local/bin/vigilant..."
chmod +x /tmp/vigilant
mv /tmp/vigilant /usr/local/bin/vigilant
ln -sf /usr/local/bin/vigilant /usr/local/bin/vigi

echo -e "${BLUE}[6/6]${NC} Finalizing logs..."
mkdir -p /var/log/vigilant
touch /var/log/vigilant/vigilant.log
chmod 666 /var/log/vigilant/vigilant.log

echo -e "\n${GREEN}✔ INSTALLATION COMPLETE${NC}"
echo -e "----------------------------------------"
vigilant -h