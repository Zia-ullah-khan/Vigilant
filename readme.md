# Vigilant

Vigilant is a lightweight, dynamic proxy server and service manager written in C++17. It allows you to route traffic to configured services, manage their lifecycle (including automatically spinning them down after a period of inactivity), and provides a dashboard for monitoring.

## Features

- **Dynamic Reverse Proxy**: Routes inbound traffic to backend services based on configured domains.
- **Service Lifecycle Management**: Automatically stops services that have been inactive for a specified timeout to save resources.
- **Dashboard Functionality**: Built-in dashboard server for monitoring and management.
- **HTTPS/SSL Support**: Native support for OpenSSL to handle secure connections, with SNI support.
- **Service Configuration**: Easily define and load services from a designated configuration directory.

## Prerequisites

- C++17 compatible compiler
- CMake (>= 3.10)
- Threads library
- OpenSSL (Optional, for Native HTTPS capabilities)

## Building the Project

1. Navigate to the project directory:
   ```bash
   cd Vigilant
   ```

2. Generate the build files using CMake:
   ```bash
   mkdir build
   cd build
   cmake ..
   ```

3. Build the project:
   ```bash
   cmake --build .
   ```

   *Note: Built executables `vigilant` and `vigilant_tests` will be generated.*

## Usage

Run the `vigilant` executable with the desired options:

```bash
Usage: vigilant [options]
  -d <dir>          Service config directory (default: /etc/vigilant/services)
  -p <port>         Listen port (default: 9000)
  -dash <port>      Dashboard listen port (default: 9001)
  -t <min>          Inactivity timeout in minutes (default: 10)
   -l <filepath>     Log file path (default: /var/log/vigilant.log, auto-falls back to user-local path)
  --cert <filepath> Global SSL certificate file
  --key <filepath>  Global SSL key file
  -h, --help        Show help message
```

### Example

```bash
./vigilant -p 8080 -dash 8081 -t 15 -d ./my_services_config
```

## Service Configuration (.vig format)

Vigilant uses custom `.vig` files to define and manage services. These configuration files use a simple key-value format and should be placed in the directory specified by the `-d` flag (default: `/etc/vigilant/services`).

A typical `.vig` file looks like this:

```ini
# Example Backend Service Definition
name = myservice
domain = api.example.com
port = 3001
type = process
command = cd /path/to/app && node server.js
pidfile = /tmp/myservice.pid
health = /health
timeout = 30
cert = /etc/letsencrypt/live/api.example.com/fullchain.pem
key = /etc/letsencrypt/live/api.example.com/privkey.pem
```

### Configuration Keys:
- `name`: The logical name of the service.
- `domain`: The domain name to route to this service. Also used for SNI matching for HTTPS.
- `port`: The internal port where the backend service listens.
- `type`: The type of service, typically `process`.
- `command`: The command used to start the service.
- `pidfile`: The path to the file where the process ID will be stored.
- `health`: The health check endpoint path (e.g., `/` or `/health`).
- `timeout`: The inactivity timeout in seconds/minutes before the service is automatically stopped.
- `cert` / `key`: Optional. Paths to the SSL certificate and private key for enabling HTTPS for this specific domain.

## Running Tests

Vigilant includes a test suite that can be run to verify functionality:

```bash
./vigilant_tests
```

## Roadmap & Vision

Vigilant is designed strictly with **performance, simplicity, and usefulness** in mind. It targets businesses, SaaS platforms, and advanced homelab users who need a dynamic, lightweight, and completely transparent proxy and service manager.

**Upcoming Features:**
- **Git-based Deployments**: Native support for deploying, building, and running services directly from Git pushes without complex CI/CD pipelines.

*Note: Vigilant is currently developed as a proprietary, closed-source solution intended for commercial licensing.*
