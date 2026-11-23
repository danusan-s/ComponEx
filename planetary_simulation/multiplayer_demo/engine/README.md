# ComponEng

![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-3.3-5586A4?logo=opengl&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-3.10%2B-064F8C?logo=cmake&logoColor=white)
![GLFW](https://img.shields.io/badge/GLFW-3.3-20232A)
![Status](https://img.shields.io/badge/Project-Learning%20Project-6C8E5E)

An ECS-based game engine built in C++ with OpenGL. A simple extensible engine providing basic features allowing for ease to build on top of.

https://github.com/user-attachments/assets/2e074537-c737-4367-95d1-6cd4d009cc22

Other examples are featured in [ComponEx](https://www.github.com/danusan-s/ComponEx). Click the thumbnail below to view the showcase.

[![Forest Showcase](https://img.youtube.com/vi/l3jfwvJUXe4/maxresdefault.jpg)](https://youtu.be/l3jfwvJUXe4)

## Overview

ComponEng is a learning project demonstrating core game engine architecture through an **Entity-Component-System (ECS)** pattern. It features archetype-based ECS design, instanced rendering, and a debug overlay.

## Why?

A personal learning project built because existing engine documentation was overwhelming. The goal is to understand how large game engines work by building a minimal, understandable system from scratch.

## Features

- Entity-Component-System architecture with archetype-based storage
- Instanced rendering with GPU instancing
- Dynamic batching and frustum culling
- Physics system with fixed timestep and collision detection
- Audio via miniaudio
- Scene serialization to JSON
- Multi-threaded system execution
- ImGui debug overlay

## Tech Stack

- **C++17** – modern language features
- **OpenGL 3.3** – rendering backend
- **GLFW** – window and input management
- **GLAD** – OpenGL function loading
- **GLM** – mathematics
- **Dear ImGui** – debug UI
- **stb_image** – texture loading
- **miniaudio** – audio playback
- **nlohmann/json** – json lib used in scene serialization
- **CMake 3.10+** – build system

## Dependencies

Most dependencies are included in the repository under third_party.
However, you will need to have the following installed on your system:
- GLM
- GLFW
- OpenGL (Mac should work as 3.3 is used)

## Building

```bash
git clone <repository-url>
cd ComponEng
make build
```

## Running the Example

```bash
./build/example/Demo
```

Controls: WASD to move, Space to move up, Shift to move down, Mouse to look, 0 to toggle cursor lock, Escape to quit.

## Tests

GoogleTest unit tests cover ECS core and collision detection. Run from the build directory:

```bash
cd build
ctest --output-on-failure
```

Or run individual test executables.

## Documentation

Deeper architecture notes are in [docs/DESIGN.md](docs/DESIGN.md), and usage documentation in [docs/USER.md](docs/USER.md).

## Future Plans

- [ ] Replace GLM with custom math library
- [ ] Vulkan renderer backend (alongside OpenGL)
- [ ] Spatial partitioning (BVH, octree) for physics broad phase
- [x] Serialization system for saving/loading scenes
- [ ] Animation system with skeletal animation
- [ ] Scripting language integration (Lua or custom DSL)
- [x] Multi-threaded system execution (thread pool + parallel queries)
- [ ] Material system with PBR shading
- [ ] Scene editor
