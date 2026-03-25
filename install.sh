#!/bin/bash
set -e

if [ "$EUID" -ne 0 ] && [[ "$OSTYPE" != "darwin"* ]]; then 
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

ARCH=$(uname -m)
OS=$(uname -s)
echo -e "${BLUE}[1/6]${NC} Detected: ${CYAN}${OS} ${ARCH}${NC}"

if [[ "$OS" == "Linux" ]]; then
    [ "$ARCH" = "x86_64" ] && BINARY="vigilant-linux-x64" || BINARY="vigilant-linux-arm64"
    FINAL_BIN="/usr/local/bin/vigilant"
elif [[ "$OS" == "Darwin" ]]; then
    [ "$ARCH" = "arm64" ] && BINARY="vigilant-macos-arm64" || BINARY="vigilant-macos-x64"
    FINAL_BIN="/usr/local/bin/vigilant"
else
    echo -e "${RED}Unsupported OS: $OS${NC}"
    exit 1
fi

URL="https://github.com/Zia-ullah-khan/Vigilant/releases/latest/download/${BINARY}"

echo -e "${BLUE}[2/6]${NC} Attempting to download pre-built binary..."
if curl -sLf -o /tmp/vigilant_bin "$URL"; then
    echo -e "${GREEN}[DONE]${NC} Binary downloaded successfully."
else
    echo -e "${BLUE}[INFO]${NC} Pre-built binary not found. Building from source..."
    
    echo -e "${BLUE}[2/6]${NC} Installing dependencies..."
    if [[ "$OS" == "Linux" ]]; then
        apt-get update -y && apt-get install -y cmake build-essential libssl-dev git
    elif [[ "$OS" == "Darwin" ]]; then
        if ! command -v brew &> /dev/null; then
            echo -e "${RED}Homebrew not found. Please install it at https://brew.sh${NC}"
            exit 1
        fi
        brew install cmake openssl git
    fi

    BUILD_DIR="/tmp/vigilant_build"
    rm -rf "$BUILD_DIR"
    git clone --depth 1 https://github.com/Zia-ullah-khan/Vigilant.git "$BUILD_DIR"
    cd "$BUILD_DIR"

    echo -e "${BLUE}[3/6]${NC} Compiling Vigilant + Unit Tests..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

    echo -e "${BLUE}[4/6]${NC} Running Unit Tests..."
    if ./build/vigilant_tests "~[integration]"; then
        echo -e "${GREEN}[PASS]${NC} All tests passed."
        cp build/vigilant /tmp/vigilant_bin
        cd /tmp
        rm -rf "$BUILD_DIR"
    else
        echo -e "${RED}[FAIL]${NC} Unit tests failed! Installation aborted.${NC}"
        rm -rf "$BUILD_DIR"
        exit 1
    fi
fi

echo -e "${BLUE}[5/6]${NC} Moving binary to ${FINAL_BIN}..."
SUDO_CMD=""
[ "$EUID" -ne 0 ] && SUDO_CMD="sudo"

$SUDO_CMD rm -f "$FINAL_BIN" 
$SUDO_CMD mv /tmp/vigilant_bin "$FINAL_BIN"
$SUDO_CMD chmod +x "$FINAL_BIN"
$SUDO_CMD ln -sf "$FINAL_BIN" /usr/local/bin/vigi

echo -e "${BLUE}[6/6]${NC} Finalizing logs..."
$SUDO_CMD mkdir -p /var/log/vigilant
$SUDO_CMD touch /var/log/vigilant/vigilant.log
$SUDO_CMD chmod 666 /var/log/vigilant/vigilant.log

echo -e "\n${GREEN}✔ INSTALLATION COMPLETE${NC}"
echo -e "----------------------------------------"
vigilant -h