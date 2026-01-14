# Distributed OCR System

A client-server OCR application built with **gRPC**, **Protobuf**, **Qt**, and **Tesseract**. This project allows parallel image processing across machines or virtual environments.

---

## Prerequisites

* **Windows** OS
* **Git**
* **CMake**
* **Visual Studio** with C++ build tools
* **vcpkg** (dependency manager for C++ libraries)

---

## Setup Instructions

### 1. Clone the Project and Remove Old Build

```bash
cd path/to/project
rm -r build
```

### 2. Clone and Bootstrap vcpkg

```bash
git clone https://github.com/microsoft/vcpkg
cd vcpkg
bootstrap-vcpkg.bat
```

### 3. Install Required Libraries

```powershell
.\vcpkg install protobuf:x64-windows grpc:x64-windows tesseract:x64-windows leptonica:x64-windows qt5-base:x64-windows
```

### 4. Generate Protobuf and gRPC Files

```bash
protoc -I. --cpp_out=. ocr.proto

protoc -I. --grpc_out=. --plugin=protoc-gen-grpc=C:\path\to\vcpkg\installed\x64-windows\tools\grpc\grpc_cpp_plugin.exe ocr.proto
```

> **Note:** Replace `C:\path\to\vcpkg` with the actual path where you installed vcpkg.

### 5. Build the Project

```bash
mkdir build
cd build

cmake .. -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake

cmake --build . --config Release
```

### 6. Run the Applications

```bash
.\Release\server.exe
.\Release\client.exe
```

---

## Project Structure

```
project/
│
├─ ocr.proto           # Protobuf definition
├─ server/             # Server-side implementation
├─ client/             # Client-side UI & logic
├─ build/              # CMake build directory
└─ vcpkg/              # vcpkg dependency manager
```

---

## Dependencies

* **Protobuf** – for message serialization
* **gRPC** – for client-server communication
* **Tesseract OCR** – for text recognition
* **Leptonica** – image processing library required by Tesseract
* **Qt5** – for client-side GUI

---

## Notes

* Ensure your **Visual Studio version matches vcpkg architecture** (`x64-windows`) when building.
* Run `server.exe` first, then `client.exe` to establish communication.
* Update the path to `grpc_cpp_plugin.exe` in the protoc command if your vcpkg installation path is different.
