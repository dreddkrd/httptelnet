# HTTP Telnet Middleware

A C++ daemon that serves as a middleware between HTTP clients and Telnet-based network equipment (Cisco, ZTE, etc.)

## Features

- **HTTP JSON Interface**: Accept configuration and commands via HTTP JSON requests
- **Persistent Telnet Connections**: Maintains long-lived connections to network devices
- **Connection Pool Management**: Manage connections to multiple devices simultaneously
- **Automatic Reconnection**: Transparently reconnects to devices if connection is lost
- **Command Queuing**: Execute commands in queue mode for multiple parallel requests
- **Pattern-based Authentication**: Support for devices with different login prompts and enable passwords
- **Thread-safe**: Multi-threaded architecture for concurrent operations
- **Automatic Cleanup**: Idle connections are cleaned up automatically

## Building

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

### Compilation

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Starting the Server

```bash
./httptelnet 8080
```

Or use default port 8080:

```bash
./httptelnet
```

## API Documentation

### 1. Connect to Device

**Endpoint:** `POST /connect`

**Request:**
```json
{
  "ip": "192.168.1.1",
  "port": 23,
  "username": "admin",
  "password": "password123",
  "username_prompt": "login:|username:",
  "password_prompt": "password:",
  "enable_password": "enable_pass",
  "enable_prompt": "Password:",
  "command_prompt": "[#$]",
  "timeout_ms": 5000
}
```

**Response (Success):**
```json
{
  "success": true,
  "error_code": 0,
  "data": {
    "ip": "192.168.1.1",
    "status": "connected"
  }
}
```

**Response (Failure):**
```json
{
  "success": false,
  "error_code": 1003,
  "error_message": "Failed to connect: Connection timed out"
}
```

### 2. Execute Commands

**Endpoint:** `POST /execute`

**Request (Single Command):**
```json
{
  "ip": "192.168.1.1",
  "commands": "show version"
}
```

**Request (Multiple Commands):**
```json
{
  "ip": "192.168.1.1",
  "commands": [
    "show version",
    "show interfaces",
    "show running-config"
  ]
}
```

**Response:**
```json
{
  "success": true,
  "error_code": 0,
  "data": [
    {
      "command_id": "0",
      "success": true,
      "output": "Cisco IOS Software, C2960 Software..."
    },
    {
      "command_id": "1",
      "success": true,
      "output": "Interface    IP-Address      OK? Method Status Protocol..."
    }
  ]
}
```

**Error Response (Device Not Connected):**
```json
{
  "success": false,
  "error_code": 1002,
  "error_message": "Device not connected. Please connect first."
}
```

### 3. Get Status

**Endpoint:** `GET /status`

**Response:**
```json
{
  "success": true,
  "error_code": 0,
  "data": {
    "active_connections": 5,
    "devices": [
      {
        "ip": "192.168.1.1",
        "status": "connected"
      },
      {
        "ip": "192.168.1.2",
        "status": "connected"
      }
    ]
  }
}
```

### 4. Disconnect Device

**Endpoint:** `POST /disconnect`

**Request:**
```json
{
  "ip": "192.168.1.1"
}
```

**Response:**
```json
{
  "success": true,
  "error_code": 0,
  "data": {
    "ip": "192.168.1.1",
    "status": "disconnected"
  }
}
```

## Error Codes

| Code | Description |
|------|-------------|
| 0 | SUCCESS |
| 1000 | INVALID_REQUEST |
| 1001 | MISSING_PARAMS |
| 1002 | DEVICE_NOT_CONNECTED |
| 1003 | CONNECTION_FAILED |
| 1004 | AUTHENTICATION_FAILED |
| 1005 | COMMAND_EXECUTION_FAILED |
| 1006 | DEVICE_NOT_FOUND |
| 1007 | INVALID_IP |
| 1008 | INTERNAL_ERROR |

## Configuration

All configuration is provided via HTTP JSON requests. There are no configuration files.

### Connection Parameters

- **ip** (required): Device IP address
- **port** (optional): Telnet port (default: 23)
- **username** (required): Login username
- **password** (required): Login password
- **username_prompt** (optional): Regex pattern for username prompt (default: "login:|username:")
- **password_prompt** (optional): Regex pattern for password prompt (default: "password:")
- **enable_password** (optional): Enable mode password
- **enable_prompt** (optional): Regex pattern for enable password prompt (default: "Password:")
- **command_prompt** (optional): Regex pattern for command prompt (default: "[#$]")
- **timeout_ms** (optional): Connection timeout in milliseconds (default: 5000)

## Examples

### Connect to Cisco Switch

```bash
curl -X POST http://localhost:8080/connect \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "username": "admin",
    "password": "cisco123",
    "enable_password": "cisco456"
  }'
```

### Execute Commands

```bash
curl -X POST http://localhost:8080/execute \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "commands": ["show version", "show interfaces"]
  }'
```

### Check Status

```bash
curl http://localhost:8080/status
```

### Disconnect Device

```bash
curl -X POST http://localhost:8080/disconnect \
  -H "Content-Type: application/json" \
  -d '{"ip": "192.168.1.10"}'
```

## Architecture

- **TelnetClient**: Low-level Telnet connection handling
- **DeviceManager**: Connection pool and device management
- **HTTPServer**: HTTP endpoint handling
- **RequestHandler**: JSON request parsing and response building

## Threading Model

- Main thread: HTTP server
- Background thread: Idle connection cleanup
- Per-request handling: Asynchronous via httplib
- Device connections: Thread-safe with mutex protection

## Performance Optimization

1. **Connection Reuse**: Connections are maintained and reused for multiple commands
2. **Pattern-based Parsing**: Efficient pattern matching for device prompts
3. **Non-blocking I/O**: Timeouts prevent hanging on slow devices
4. **Connection Pooling**: Unlimited simultaneous device connections
5. **Automatic Cleanup**: Idle connections are cleaned every 10 minutes

## License

MIT
