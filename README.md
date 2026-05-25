# HTTP Telnet Middleware

A high-performance C++ daemon that serves as a middleware between HTTP clients and Telnet-based network equipment (Cisco, ZTE, etc.). Features persistent connections with automatic keepalive and intelligent nested command execution.

## Features

- **HTTP JSON Interface**: Accept configuration and commands via HTTP JSON requests
- **Persistent Telnet Connections**: Maintains long-lived connections to network devices with configurable keepalive
- **Automatic Reconnection**: Transparently reconnects and re-authenticates if connection is lost
- **Nested Command Navigation**: Support for hierarchical device configuration (e.g., configure → interface → commands)
- **Intelligent Prompt Detection**: Captures and validates complete prompt lines including device context
- **Pattern-based Authentication**: Flexible login prompts and enable passwords with regex patterns
- **Terminal Control**: Automatic pagination disable command execution after authentication
- **Thread-safe**: Multi-threaded architecture for concurrent operations across unlimited devices
- **No Configuration Files**: All settings provided via HTTP JSON requests

## Building

### Prerequisites

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git
```

### Compilation

```bash
git clone https://github.com/dreddkrd/httptelnet.git
cd httptelnet

# Download dependencies
mkdir -p third_party && cd third_party
git clone https://github.com/yhirose/cpp-httplib.git
git clone https://github.com/nlohmann/json.git
cd ..

# Build
mkdir build && cd build
cmake ..
make -j4
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

Establishes a connection to a network device and keeps it persistent.

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
  "terminal_nopage": "terminal length 0",
  "timeout_ms": 5000,
  "keepalive_interval_ms": 30000,
  "keepalive_command": "\n"
}
```

**Parameters:**
- **ip** (required): Device IP address
- **port** (optional): Telnet port (default: 23)
- **username** (required): Login username
- **password** (required): Login password
- **username_prompt** (optional): Regex pattern for username prompt (default: `login:|username:`)
- **password_prompt** (optional): Regex pattern for password prompt (default: `password:`)
- **enable_password** (optional): Enable mode password
- **enable_prompt** (optional): Regex pattern for enable password prompt (default: `Password:`)
- **command_prompt** (optional): Regex pattern for command prompt (default: `[#$]`)
- **terminal_nopage** (optional): Command to disable pagination (e.g., `terminal length 0`, `terminal pager disabled`)
- **timeout_ms** (optional): Connection timeout in milliseconds (default: 5000)
- **keepalive_interval_ms** (optional): Keepalive interval in milliseconds (default: 30000 = 30 seconds)
- **keepalive_command** (optional): Command to send for keepalive (default: `\n`)

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

### 2. Execute Commands with Navigation

**Endpoint:** `POST /execute`

Executes commands within nested device contexts (e.g., configuration modes, interfaces). The middleware automatically navigates to the specified path, executes commands, and returns to root level.

**Request:**
```json
{
  "ip": "192.168.1.1",
  "segments": [
    {
      "path": ["configure terminal"],
      "commands": ["hostname NewRouter"]
    },
    {
      "path": ["configure terminal", "interface Ethernet1"],
      "commands": [
        "description Management Interface",
        "ip address 10.0.0.1 255.255.255.0",
        "no shutdown"
      ]
    },
    {
      "path": [],
      "commands": ["write memory", "show running-config"]
    }
  ]
}
```

**Request Structure:**

Each segment contains:
- **path** (array): Navigation commands to enter nested contexts
  - Each command changes the device prompt
  - Middleware waits for new prompt after each navigation command
  - Commands are executed in order
- **commands** (array): Actual commands to execute within the current context
  - Executed after successfully navigating to the specified path
  - Output is captured for each command

**Execution Flow:**

1. **Navigate** - Execute commands from `path` in sequence
2. **Configure** - Execute commands from `commands`
3. **Exit** - Send `exit` command N times (where N = number of path commands)
4. **Return to root** - Ready for next segment

**Example Execution (from above request):**

```
1. Segment 1:
   - Execute: "configure terminal"
   - Execute: "hostname NewRouter"
   - Execute: exit (back to root)

2. Segment 2:
   - Execute: "configure terminal"
   - Execute: "interface Ethernet1"
   - Execute: "description Management Interface"
   - Execute: "ip address 10.0.0.1 255.255.255.0"
   - Execute: "no shutdown"
   - Execute: exit (from interface)
   - Execute: exit (from config)

3. Segment 3:
   - Execute: "write memory"
   - Execute: "show running-config"
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
      "output": "[navigation and configuration output]"
    },
    {
      "command_id": "1",
      "success": true,
      "output": "[interface configuration output]"
    },
    {
      "command_id": "2",
      "success": true,
      "output": "[write memory and show running-config output]"
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

Returns information about active connections.

**Response:**
```json
{
  "success": true,
  "error_code": 0,
  "data": {
    "active_connections": 3,
    "devices": [
      {
        "ip": "192.168.1.1",
        "status": "connected"
      },
      {
        "ip": "192.168.1.2",
        "status": "connected"
      },
      {
        "ip": "192.168.1.3",
        "status": "connected"
      }
    ]
  }
}
```

### 4. Disconnect Device

**Endpoint:** `POST /disconnect`

Closes connection to a specific device.

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
|------|----------------|
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

## Advanced Features

### Persistent Connections

- Connections are maintained between requests
- Configurable keepalive mechanism sends periodic commands (default: newline every 30 seconds)
- Automatic reconnection and re-authentication on connection loss
- No connection idle timeout - connections persist indefinitely

### Automatic Reconnection

- When a command is executed on a disconnected device, the middleware automatically:
  1. Detects connection loss
  2. Closes the broken socket
  3. Reconnects to the device
  4. Re-authenticates using saved credentials
  5. Executes the requested command

### Intelligent Prompt Handling

- **Full Line Prompts**: Captures entire prompt line including device context
  - Example: `Router(config-if)#` instead of just `#`
  - Prevents misidentification of `#` in command output
  
- **Hierarchical Prompt Updates**: Tracks prompt changes as device navigation changes
  - Each context level has its own prompt signature
  - Middleware caches current prompt to properly identify command completion

- **Output Preservation**: Ensures complete command output is captured
  - Waits for full prompt line to appear at end of output
  - Removes only the final prompt line from output
  - Preserves intermediate prompts in output

### Pagination Control

- **terminal_nopage Parameter**: Disables pagination after authentication
  - For Cisco: `terminal length 0` or `terminal pager disabled`
  - For ZTE: Similar commands depending on device
  - Ensures full output is returned without user interaction

## Examples

### Example 1: Simple Configuration

```bash
curl -X POST http://localhost:8080/connect \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "username": "admin",
    "password": "cisco123",
    "enable_password": "cisco456",
    "terminal_nopage": "terminal length 0"
  }'
```

### Example 2: Nested Interface Configuration

```bash
curl -X POST http://localhost:8080/execute \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "segments": [
      {
        "path": ["configure terminal", "interface GigabitEthernet0/0/0"],
        "commands": [
          "description WAN Interface",
          "ip address 203.0.113.1 255.255.255.0",
          "no shutdown"
        ]
      }
    ]
  }'
```

### Example 3: Multiple Configurations in Sequence

```bash
curl -X POST http://localhost:8080/execute \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "segments": [
      {
        "path": ["configure terminal"],
        "commands": [
          "hostname CoreRouter",
          "ip domain-name example.com"
        ]
      },
      {
        "path": ["configure terminal", "interface Loopback0"],
        "commands": [
          "ip address 1.1.1.1 255.255.255.255",
          "description Loopback"
        ]
      },
      {
        "path": [],
        "commands": ["write memory"]
      }
    ]
  }'
```

### Example 4: Show Commands

```bash
curl -X POST http://localhost:8080/execute \
  -H "Content-Type: application/json" \
  -d '{
    "ip": "192.168.1.10",
    "segments": [
      {
        "path": [],
        "commands": [
          "show version",
          "show interfaces",
          "show running-config"
        ]
      }
    ]
  }'
```

### Example 5: Status Check

```bash
curl http://localhost:8080/status
```

### Example 6: Disconnect

```bash
curl -X POST http://localhost:8080/disconnect \
  -H "Content-Type: application/json" \
  -d '{"ip": "192.168.1.10"}'
```

## Architecture

- **TelnetClient**: Low-level Telnet connection handling with persistent keepalive
- **DeviceManager**: Connection pool management and device lifecycle
- **HTTPServer**: HTTP endpoint routing (cpp-httplib)
- **RequestHandler**: JSON request parsing and response building

## Threading Model

- **Main Thread**: HTTP server event loop
- **Per-Device Keepalive Thread**: Sends keepalive commands periodically
- **Per-Request Handling**: Asynchronous via httplib
- **Connection Pool**: Thread-safe with mutex protection

## Performance Characteristics

- **Connection Reuse**: Single persistent connection per device
- **Zero Connection Overhead**: No reconnection delays for subsequent commands
- **Efficient I/O**: Non-blocking sockets with configurable timeouts
- **Memory Efficient**: Streaming output processing
- **Scalability**: Unlimited simultaneous device connections
- **Automatic Recovery**: Transparent reconnection without application knowledge

## Configuration via HTTP

All configuration is provided via HTTP JSON requests at runtime:

1. **First Request**: Client sends `/connect` with device parameters
   - Connection is established and cached
   - Keepalive thread starts automatically

2. **Subsequent Requests**: Client sends `/execute` with commands
   - Middleware uses cached connection
   - Automatic reconnection if needed

3. **Connection Lifetime**: Persists until:
   - Explicit `/disconnect` is called
   - Device closes connection (automatic reconnect on next command)
   - Middleware is restarted

## Troubleshooting

### Connection Timeout
- Increase `timeout_ms` parameter
- Check network connectivity to device
- Verify device Telnet service is running

### Prompt Not Recognized
- Verify `command_prompt` regex pattern
- Use broader pattern if needed (e.g., `.*[#$]` for any prompt ending with # or $)
- Enable debug logging to see actual prompts received

### Pagination Issues
- Set `terminal_nopage` parameter with appropriate command for your device
- Examples:
  - Cisco: `terminal length 0`
  - ZTE: `terminal pager disabled`

### Connection Drops
- Increase `keepalive_interval_ms` if device is aggressive about idle timeouts
- Ensure network doesn't have strict idle timeouts
- Check device for connection limits

## License

MIT
