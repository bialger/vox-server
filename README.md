# vox-server

Server side of the Vox messenger, powered by Boost and C++.

The main Boost configuration is found in the [correspondent](lib/boost/CMakeLists.txt) CMakeLists.txt file.
The main Google Tests configuration is found in the [correspondent](tests/CMakeLists.txt) CMakeLists.txt file.

## Prerequisites

* CMake
* Ninja
* Git

## How to build and run

Run the following commands from the project directory.

1. Create CMake cache

```shell
cmake -S . -B cmake-build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
```

2. Build executable target

```shell
cmake --build cmake-build --target vox-server
```

3. Run executable target

* On Windows:

```shell
.\cmake-build\bin\vox-server.exe
```

* On *nix:

```shell
./cmake-build/bin/vox-server
```
