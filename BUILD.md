# Build Instructions

## Quick Start

```bash
# Clone the repository
git clone https://github.com/dreddkrd/httptelnet.git
cd httptelnet

# Download dependencies
mkdir -p third_party
cd third_party

# Download cpp-httplib
git clone https://github.com/yhirose/cpp-httplib.git

# Download nlohmann/json
git clone https://github.com/nlohmann/json.git

cd ..

# Build
mkdir build
cd build
cmake ..
make -j4

# Run
./httptelnet 8080
```

## System Requirements

- Linux (x86_64)
- GCC 9+ or Clang 10+
- CMake 3.15+
- pthreads (included in glibc)

## Troubleshooting

### CMake not found

```bash
sudo apt-get install cmake
```

### Build fails with missing headers

Ensure third_party libraries are properly downloaded:

```bash
ls -la third_party/
# Should show: cpp-httplib/ and json/
```

### Port already in use

Use a different port:

```bash
./httptelnet 9090
```

### Connection timeout issues

Increase timeout_ms in connection parameters or check network connectivity.
