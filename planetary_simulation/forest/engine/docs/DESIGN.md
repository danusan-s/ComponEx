# ComponEng Design Document

## Overview

ComponEng is an archetype-based Entity-Component-System (ECS) game engine built in C++17. The architecture is designed around three core pillars:

1. **ECS Core** - Data-oriented entity management with cache-efficient archetype storage
2. **Rendering** - OpenGL 3.3 instanced rendering with dynamic batching
3. **Physics** - Fixed-timestep simulation with impulse-based collision resolution

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Engine                                  │
│          (owns the World and the plugins, runs the loop)        │
├─────────────────────────────────────────────────────────────────┤
│  plugins: Window, Input, Physics, Camera, Audio, Render, Game   │
│  runner:  windowed loop (WindowPlugin) or fixed-rate headless   │
└─────────────────────────────────────────────────────────────────┘
           │
           ▼
┌─────────────────────────────────────────────────────────────────┐
│                         World                                   │
│                 (Central ECS Coordinator)                       │
├─────────────────────────────────────────────────────────────────┤
│  ComponentRegistry    │  EntityManager                          │
│  ArchetypeManager     │  SystemManager                          │
│  ThreadPool           │  EventBus                               │
│  ResourceManager      │                                         │
└─────────────────────────────────────────────────────────────────┘
```

## Core Subsystems

### 1. Engine and plugins (`core/engine.hpp`, `core/plugin.hpp`)

The `Engine` owns the `World` and a list of plugins and drives the frame loop. On
its own it is a bare world; everything else is a plugin (`IPlugin`):

- **WindowPlugin**: GLFW window, the render device, ImGui, and the windowed main loop (becomes the engine's runner)
- **InputPlugin**: `InputState`/`ActionState`, `InputSystem` (PreUpdate), `InputBufferSystem` (FrameEnd)
- **PhysicsPlugin**, **CameraPlugin**, **AudioPlugin**: their components, resources and systems
- **RenderPlugin**: mesh/material/light components, the `AssetManager` with the built-in assets, and the LOD, culling, batching and render systems. Requires `WindowPlugin`
- **DefaultPlugins**: all of the above, with per-plugin `disable<>()` and `window` config
- **NetPlugin** / **NatPlugin** (separate libraries): networking, and internet play on top of it

A plugin has three phases: `build(Engine&)` when added (register components,
resources, systems), `ready(World&)` once all are built (open, load, create
entities) and `cleanup(World&)` in reverse order after the systems are destroyed.
`engine.require<P>()` inside `build` pulls in a dependency first.

**Key Methods**:
```cpp
Engine& add(Plugin);   // build it now; throws on a duplicate
void require<P>();     // inside build(): make sure P is built first
void initialize();     // every plugin's ready(), then create systems
void update(double);   // one frame of systems
void shutdown();       // destroy systems, cleanup plugins in reverse
void run();            // initialize, runner, shutdown
void setRunner(fn);    // replace the frame loop
```

### 2. World (`ecs/world.hpp`)

The `World` class is the central coordinator that provides the public API for all ECS operations:

**Owned Subsystems**:
| Manager | Purpose |
|---------|---------|
| `ComponentRegistry` | Maps C++ types to compact ComponentIDs |
| `EntityManager` | Entity creation/destruction, ID recycling |
| `ArchetypeManager` | Creates/stores archetypes by signature |
| `SystemManager` | System registration and execution ordering |
| `ThreadPool` | Parallel task execution for systems |
| `EventBus` | Systems can emit and retrieve events |
| `ResourceManager` | Singleton resources (InputState, MainCamera) |

**Key Template Methods**:
```cpp
EntityID createEntity();                    // Create new entity
void destroyEntity(EntityID);                // Destroy entity
void addComponent(EntityID, T);              // Add component to entity
void removeComponent<T>(EntityID);           // Remove component
bool hasComponent<T>(EntityID);              // Check component presence
T& getComponent<T>(EntityID);               // Mutable reference (bumps column version)
const T& readComponent<T>(EntityID);        // Read-only reference (no version bump)
void registerComponent<T>();                 // Register component type
void registerSystem<T>(group);               // Register system
Query<> query();                             // Start a query; chain .read<>() / .write<>()
void emit_event<T>(event);                   // Emit typed event
T& get_resource<T>();                        // Get singleton resource
```

### 3. Entity Management (`ecs/entity.hpp`)

**Type Definitions**:
```cpp
using EntityID    = std::uint32_t;   // Unique entity identifier
using ComponentID = std::uint8_t;    // Component type identifier
using ArchetypeID = std::uint8_t;    // Archetype identifier
using Signature   = std::bitset<MAX_COMPONENTS>;  // Component presence mask
```

**Constraints**:
- `MAX_ENTITIES = 10000` - Maximum concurrent entities
- `MAX_COMPONENTS = 32` - Maximum distinct component types
- `MAX_ARCHETYPES = 64` - Maximum unique archetype combinations
- Entity ID 0 is reserved as `INVALID_ENTITY`

### 4. EntityManager (`ecs/entity_manager.hpp`)

Manages entity lifecycle with a free-list for ID recycling:

```
┌────────────────────────────────────────┐
│           EntityManager                │
├────────────────────────────────────────┤
│  m_freeIDs: queue<EntityID>            │
│  m_entityRecords: array<EntityRecord>  │
│  m_livingEntityCount: uint32           │
└────────────────────────────────────────┘
```

**EntityRecord** (per-entity metadata):
```cpp
struct EntityRecord {
    std::size_t row;      // Row index in current archetype
    Signature signature; // Bitmask of present components
};
```

### 5. ComponentRegistry (`ecs/component_registry.hpp`)

Maps C++ types to runtime component IDs:

```
┌─────────────────────────────────────────┐
│         ComponentRegistry               │
├─────────────────────────────────────────┤
│  m_typeToID: unordered_map<type_index,  │
│              ComponentID>               │
│  m_componentInfos: array<ComponentInfo> │
│  m_nextComponentID: ComponentID         │
└─────────────────────────────────────────┘
```

**ComponentInfo** (runtime metadata per type):
```cpp
struct ComponentInfo {
    const char* name;        // Type name
    size_t size;            // sizeof(T)
    size_t alignment;       // alignof(T)
    vector<Field> fields;   // For serialization
};
```

### 6. Archetype System

The archetype system provides cache-efficient component storage:

#### ArchetypeManager (`ecs/archetype_manager.hpp`)

```cpp
class ArchetypeManager {
    array<Archetype, MAX_ARCHETYPES> m_archetypes;
    unordered_map<Signature, ArchetypeID> m_signatureToArchetypeID;
};
```

#### Archetype (`ecs/archetype.hpp`)

Entities with identical component sets are grouped into archetypes:

```
┌─────────────────────────────────────────────────┐
│                   Archetype                     │
├─────────────────────────────────────────────────┤
│  m_columns: vector<ComponentColumn>             │
│  m_rowToEntity: vector<EntityID>                │
│  m_signature: Signature                         │
└─────────────────────────────────────────────────┘
```

**ComponentColumn** - Contiguous storage for one component type:
- Uses aligned allocation for proper type alignment
- Stores components as raw bytes with calculated stride
- Uses swap-remove for O(1) removal
- Carries a monotonic `ColumnVersion` (`uint64_t`, starts at 0) that is bumped
  whenever the column is, or may have been, written

**Column versions and access** - an archetype hands out columns two ways:
```cpp
const ComponentColumn& readColumn(ComponentID) const;  // no version change
ComponentColumn&       writeColumn(ComponentID);       // bumps the version
ColumnVersion          columnVersion(ComponentID) const;
```
Adding or removing a row also bumps every affected column. A consumer that
remembers the version it last saw can detect "something wrote to this column"
by comparing for inequality. Versions are per column, per archetype: they say
*that* a column was written, not which rows. The counter is not atomic; it is
only bumped on the thread that starts a write, never inside a parallel loop.

**Entity Movement Flow**:
```
Entity with signature {Transform, Mesh} 
         │
         ▼ addComponent(RigidBody)
         │
         ▼ creates/moves to new archetype
         │
    New archetype {Transform, Mesh, RigidBody}
```

### 7. Query System (`ecs/query.hpp`)

Type-safe iteration over entities matching component requirements. A query
declares, per component, whether it only *reads* it or may *write* it:

```cpp
// Transform is read-only, Mesh may be modified
auto query = world.query()
                 .read<TransformComponent>()
                 .write<MeshComponent>();

// Callback parameters follow declaration order:
// `const T&` for read<>, `T&` for write<>
query.each([](const TransformComponent& t, MeshComponent& m) {
    // Process entity
});

// Iterate with EntityID
query.eachWithEntity([](EntityID e, const TransformComponent& t, MeshComponent& m) {
    // Process entity with ID
});

// Parallel execution using ThreadPool
query.eachParallel(threadPool, [](const TransformComponent& t, MeshComponent& m) {
    // Process in parallel
});

// Exclude entities with certain components (does not appear in the callback)
query.exclude<ColliderComponent>();
```

`read<>` and `write<>` take any number of types and can be chained in any
order. A component may be declared only once; to read and write the same type,
declare it `write<>` (declaring it twice is a compile error).

**Versioning.** Each time a query runs, every `write<>` column of every
matching, non-empty archetype has its version bumped once (not per row), up
front on the calling thread. `read<>` columns are never touched, and neither
are archetypes the query does not match or that have no entities. For range-for
iteration the bump happens when iteration enters an archetype.

**Chunks.** `eachChunk(fn)` (and `eachChunkParallel(pool, fn)`) hand the
callback one archetype at a time instead of one entity at a time, as a
`QueryChunk`. It exposes what only the archetype knows: `size()` (rows),
`archetypeId()`, `entity(row)`, `version<T>()` (T's current column version), and
`data<T>()`, T's column as a contiguous array indexed by row (`const T*` for
`read<>`, `T*` for `write<>`). Loop `for (row = chunk.begin(); row <
chunk.end(); ++row)`; in the parallel variant each task gets a sub-range.

```cpp
world.query().read<TransformComponent>().write<MeshComponent>().eachChunk(
    [](const auto& chunk) {
        const TransformComponent* t = chunk.template data<TransformComponent>();
        MeshComponent* m = chunk.template data<MeshComponent>();
        for (size_t row = chunk.begin(); row < chunk.end(); ++row) { /* ... */ }
    });
```

A write declaration is a statement of intent: the version is bumped even if the
callback ends up not changing anything. Declare `read<>` wherever you only look.

#### Transform matrix cache (`renderer/transform_matrix_cache.hpp`)

Culling and batching both need each renderable's model matrix. The shared
`TransformMatrixCache` (a World resource, filled by `refreshTransformMatrices`)
keeps those matrices per archetype, row-aligned with the Transform column, and
remembers the Transform column version they were built from:

- **Version unchanged** - nothing wrote the column or added/removed a row, so
  every matrix is valid. The archetype costs one integer compare, however many
  entities it holds.
- **Version changed** - the version is per column, not per entity, so it cannot
  say which rows moved. Each row's nine transform floats are compared with what
  its matrix was built from, and only differing rows are rebuilt.

The skip relies on write access bumping the version *before* data is touched
(`write<>()` queries, `getComponent<T>()`, `addComponent`, ...). Keeping a
`TransformComponent&` across frames and writing through it later bypasses that.
Anything that only inspects a transform should use `read<>()` or
`readComponent<T>()` so it does not invalidate the cache for its archetype.

### 8. System Architecture (`ecs/system.hpp`, `ecs/system_manager.hpp`)

#### System Interface
```cpp
class ISystem {
public:
    virtual void onUpdate(const SystemState& state) = 0;
    virtual void onCreate(const SystemState& state);  // Optional
    virtual void onDestroy(const SystemState& state); // Optional
};

struct SystemState {
    World* world;       // Pointer to world for queries
    float deltaTime;   // Frame delta time
};
```

#### System Groups (Execution Order)
```cpp
enum class SystemGroup {
    PreUpdate,     // Runs first, e.g., InputSystem, networking
    Simulation,    // Physics, logic, e.g., CameraSystem, PhysicsSystem
    Presentation,  // Culling, batching, rendering, e.g., RenderSystem
    FrameEnd       // Rolls the frame over, e.g., InputBufferSystem
};
```

#### SystemManager
- Prevents duplicate system registration
- Creates all systems with world reference
- Updates all systems each frame in group order
- Destroys all systems on shutdown

### 9. Thread Pool (`ecs/thread_pool.hpp`)

Fixed-size worker thread pool for parallel task execution:

```cpp
class ThreadPool {
    vector<thread> m_workers;
    queue<function<void()>> m_tasks;
};
```

Used by Query::eachParallel() for distributing entity processing across threads.

### 10. Events (`events/event_bus.hpp`)

Double-buffered event system:

```cpp
class EventBus {
    unordered_map<type_index, unique_ptr<IEventQueue>> m_queues;
};
```

**EventQueue** (per-event-type):
```cpp
template<typename EventType> class EventQueue {
    vector<EventType> m_read;   // Read buffer
    vector<EventType> m_write;  // Write buffer
};
```

Usage:
```cpp
world.emit_event<CollisionEvent>(event);
const auto& events = world.get_events<CollisionEvent>();
```

---

## Components

| Component | Header | Fields |
|-----------|--------|--------|
| `TransformComponent` | `components/transform_component.hpp` | `position`, `rotation`, `scale` (Vec3 each) |
| `MeshComponent` | `components/mesh_component.hpp` | `meshID`, `visible` |
| `MaterialComponent` | `components/material_component.hpp` | `materialName`, `materialID` |
| `CameraComponent` | `components/camera_component.hpp` | `fov`, `aspectRatio`, `nearPlane`, `farPlane`, `viewProjectionMatrix` |
| `RigidBodyComponent` | `components/rigidbody_component.hpp` | `type` (Static/Dynamic/Kinematic), `velocity`, `mass`, `restitution` |
| `ColliderComponent` | `components/collider_component.hpp` | `type` (Box/Sphere), `transform` |

---

## Systems

### InputSystem (`systems/input_system.hpp`)
- **Group**: PreUpdate
- **Purpose**: Polls raw GLFW input, updates InputComponent and MouseInputComponent
- **Runs**: Before simulation systems

### CameraSystem (`systems/camera_system.hpp`)
- **Group**: Simulation
- **Purpose**: WASD movement, mouse look, computes view/projection matrices

### PhysicsSystem (`physics/physics_system.hpp`)
- **Group**: Simulation
- **Purpose**: Fixed 60Hz timestep, gravity integration, collision detection/response
- **Features**:
  - Accumulator pattern for fixed timestep
  - Impulse-based collision resolution
  - Parallel gravity integration via ThreadPool

### RenderSystem (`renderer/render_system.hpp`)
- **Group**: Presentation
- **Purpose**: Renders all visible entities with batching
- **Features**:
  - Frustum culling
  - Dynamic batching by (mesh, texture, shader)
  - GPU instancing via glDrawElementsInstanced

---

## Rendering Pipeline

### Architecture
```
┌─────────────────────────────────────────────────────────────┐
│                      BatchingSystem                         │
│  one pass over Transform + Mesh + Material entities         │
├─────────────────────────────────────────────────────────────┤
│  material->writeInstanceData() writes each entity straight  │
│  into the flat buffer of its (mesh, material) batch         │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                       RenderQueue                           │
│  pooled RenderBatch { mesh, material, shader, texture,      │
│                       instanceCount, instanceData[] }       │
├─────────────────────────────────────────────────────────────┤
│  sorted by shader, texture, material, mesh                  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                       RenderSystem                          │
│  skips redundant shader / uniform / texture binds between   │
│  consecutive batches, uploads each batch, draws instanced   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                   IRenderDevice                             │
│              (Abstract Rendering API)                       │
├─────────────────────────────────────────────────────────────┤
│  createShader(), createTexture(), createMesh(),            │
│  createBuffer(), setupInstanceAttributes(),                │
│  drawIndexedInstanced()                                    │
└─────────────────────────────────────────────────────────────┘
                            │
                    ┌───────┴───────┐
                    ▼               ▼
┌────────────────────────┐  ┌────────────────────────┐
│    GLRenderDevice      │  │  (Future: Vulkan impl)  │
│    (OpenGL 3.3)        │  │                        │
└────────────────────────┘  └────────────────────────┘
```

### Rendering Flow
1. **Query**: Query all entities with TransformComponent, MeshComponent, MaterialComponent
2. **LOD**: `LodSystem` picks each `LodComponent` entity's mesh from its distance
   to the camera, or from the fraction of the screen's height it covers when
   the component has a `size` (or hides it beyond the last level). A level
   change cross-fades over `RenderSettings::lodFadeSeconds` when `lodDither` is
   on: `LodSystem` puts the old mesh in `MeshComponent::fadeMeshID` and a
   signed `fade`, and `BatchingSystem` writes two instances, one per mesh,
   whose `instanceFade` values are complementary. The fragment shader discards
   on a screen-space dither pattern, so the two meshes tile the pixels between
   them. Materials whose shader lacks `instanceFade` keep the hard switch
3. **Culling**: Test each entity's AABB against view frustum planes
4. **Batching**: Group visible entities by (meshID, materialID); each entity's
   instance data is written directly into its batch's flat buffer, with the model
   matrix built by `TransformComponent::toMatrix()` and cached per entity while
   the transform is unchanged. An entity with a `ProceduralInstancesComponent`
   becomes one data-less batch of `count` instances instead (the shader places
   them from `gl_InstanceID`); the render step skips its buffer upload and
   instance attributes
5. **Sorting**: Order batches opaque first, then by (shaderID, textureID,
   materialID, meshID) so draws sharing GPU state are adjacent. "Opaque" is the
   material's `MaterialType` (`Opaque` and `Cutout`)
6. **Upload**: Write each batch's instance data to its GPU buffer, once
7. **Depth pre-pass** (`RenderSettings::depthPrepass`): Draw the opaque and
   `Cutout` batches with each shader's depth variant (its vertex stage plus an
   empty fragment shader, `Shader::depthVariant`), colour writes off. A `Cutout`
   material gets `Shader::alphaTestedDepthVariant` instead: the shader's own
   fragment stage compiled with `COMPONENG_DEPTH_PREPASS` defined, with the
   albedo bound, so its `discard` cuts the same holes in depth as in colour
   (a batch with LOD-fading instances gets this variant too, for the same reason)
8. **Opaque colour pass**: Draw the opaque batches again, shaded, with depth
   test `Equal` and depth writes off, so only the nearest surface at each pixel
   runs the fragment shader. Then the sky
9. **Transparent pass**: Draw `Transparent` batches with their own `RenderState`
10. **Post-process** (`RenderSettings::antiAliasing`, `renderScale`): With FXAA
   on, or a render scale other than 1, steps 7 to 9 are drawn into an off-screen
   `IRenderTarget` (the window's viewport size times the scale), and
   `PostProcessPass` copies it onto the window through the `post_process`
   shader, smoothing the edges when FXAA is on. Otherwise, or without a
   `post_process` shader, the scene is drawn straight to the window
11. **Draw** (both passes): Bind the material's extra textures, texture buffers and uniform
   blocks (`IMaterial::setTexture` / `setBufferTexture` / `setUniformBuffer`),
   and use glDrawElementsInstanced for
   single-draw-call rendering, skipping shader/texture rebinds that did not change

### RenderQueue (`renderer/batching/render_queue.hpp`)

```cpp
struct RenderBatch {
    HandleID meshID, materialID, shaderID, textureID;
    uint32_t instanceCount;
    std::vector<float> instanceData;  // instanceCount * stride floats, packed
};
```

The queue's batches live in a pool that survives `clear()`, so a steady scene
builds its queue without allocating. Only batches that received at least one
instance this frame exist.

### Instance Buffer Layout
The layout is reflected from the shader's instance attributes (location >= 3).
For the built-in diffuse shader that is 4 vec4 (model matrix) + 1 vec4 (color):
- stride = 80 bytes
- vertexAttribDivisor = 1 (per-instance)

---

## Physics System

### Collision Detection (`physics/collision_detection.hpp`)

**Supported Tests**:
| Test | Function |
|------|----------|
| AABB vs AABB | `testBoxBox()` |
| Sphere vs Sphere | `testSphereSphere()` |
| AABB vs Sphere | `testBoxSphere()` |
| Sphere vs AABB | `testSphereBox()` (reversed normal) |

**CollisionInfo**:
```cpp
struct CollisionInfo {
    Vec3 normal;    // Direction from A to B
    float penetration;  // Overlap depth
};
```

### Physics Flow
```
1. Query all entities with TransformComponent, RigidBodyComponent
2. Apply gravity (parallel via ThreadPool)
3. Integrate velocity: position += velocity * dt
4. Broad-phase: Simple O(n²) (future: spatial partitioning)
5. Narrow-phase: Dispatch to appropriate collision test
6. Response: Impulse-based with position correction
```

### Fixed Timestep
```cpp
double accumulator = 0.0;
while (accumulator >= FIXED_DT) {
    physicsSystem.update(FIXED_DT);
    accumulator -= FIXED_DT;
}
```

---

## Resources

### ResourceManager (`resources/resource_manager.hpp`)

Singleton/shared resources accessible by systems:

```cpp
world.set_resource(InputState{...});
world.set_resource(MainCamera{...});

InputState& input = world.get_resource<InputState>();
MainCamera& cam = world.get_resource<MainCamera>();
```

### AssetManager (`renderer/asset_manager.hpp`)

Static caching for loaded assets:

```
┌─────────────────────────────────────────────┐
│              AssetManager                   │
├─────────────────────────────────────────────┤
│  s_shaders: map<string, ShaderID>          │
│  s_textures: map<string, TextureID>       │
│  s_meshes: map<string, MeshID>            │
│  s_shaderResources: map<ShaderID, unique_ptr<Shader>>   │
│  s_textureResources: map<TextureID, unique_ptr<Texture2D>>│
│  s_meshResources: map<MeshID, unique_ptr<Mesh>>         │
└─────────────────────────────────────────────┘
```

Loads assets on first request, caches by string name, returns const references.

---

## Render Device Abstraction

### IRenderDevice (`renderer/api/irender_device.hpp`)

Abstract interface for GPU operations:

```cpp
class IRenderDevice {
    virtual void init(void* windowHandle) = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void clear(float r, float g, float b, float a) = 0;
    virtual void present(void* windowHandle) = 0;

    virtual unique_ptr<IShader> createShader() = 0;
    virtual unique_ptr<ITexture> createTexture() = 0;
    virtual unique_ptr<IMesh> createMesh() = 0;
    virtual unique_ptr<IBuffer> createBuffer() = 0;

    virtual void setupInstanceAttributes(IBuffer&) = 0;
    virtual void drawIndexedInstanced(size_t, uint32_t) = 0;
};
```

### Sub-interfaces

| Interface | Purpose |
|-----------|---------|
| `IShader` | Compiled shader program, uniform setters |
| `ITexture` | 2D texture, bind/generate |
| `IMesh` | Vertex/index data upload, bind for rendering |
| `IBuffer` | GPU buffer (vertex/index/uniform/instance) |

### GLRenderDevice (`renderer/opengl/gl_render_device.hpp`)

OpenGL 3.3 Core Profile implementation. Creates OpenGL-backed resources, manages global GL state.

---

## Data Flow Diagram

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ Plugin build│────▶│  Register   │────▶│   Create    │
│ (IPlugin)   │     │Components   │     │   Systems   │
└─────────────┘     └─────────────┘     └─────────────┘
                                              │
                    ┌─────────────────────────┼─────────────────────────┐
                    │                         │                         │
                    ▼                         ▼                         ▼
             ┌──────────────┐           ┌────────────┐           ┌────────────┐
             │  PreUpdate   │           │Simulation  │           │Presentation│
             │   Systems    │           │   Systems  │           │   Systems  │
             └──────────────┘           └────────────┘           └────────────┘
                    │                         │                         │
                InputSystem              CameraSystem              RenderSystem
                                        PhysicsSystem
                    │                         │                         │
                    ▼                         ▼                         ▼
              ┌────────────┐           ┌────────────┐           ┌────────────┐
              │InputState  │           │  Collision │           │   Render   │
              │Resource    │           │   Events   │           │   Output   │
              └────────────┘           └────────────┘           └────────────┘
```

---

## Class Relationship Summary

| Class | Owned By | Dependencies |
|-------|----------|--------------|
| `Engine` | - | `World`, `Window`, `IRenderDevice` |
| `World` | `Engine` | All ECS managers |
| `ComponentRegistry` | `World` | `Entity` types |
| `EntityManager` | `World` | `EntityRecord` |
| `ArchetypeManager` | `World` | `Archetype` |
| `Archetype` | `ArchetypeManager` | `ComponentColumn`, `AlignedBuffer` |
| `SystemManager` | `World` | `ISystem` |
| `ThreadPool` | `World` | std::thread |
| `EventBus` | `World` | `EventQueue` |
| `ResourceManager` | `World` | `IResource` |
| `Window` | `Engine` | `GLFW`, `IRenderDevice` |
| `GLRenderDevice` | `Engine` | OpenGL 3.3 |
| `RenderSystem` | `World` | `RenderQueue` |
| `PhysicsSystem` | `World` | `CollisionDetection` |

---

## File Structure

```
include/componeng/
├── components/
│   ├── camera_component.hpp
│   ├── collider_component.hpp
│   ├── material_component.hpp
│   ├── mesh_component.hpp
│   ├── rigidbody_component.hpp
│   └── transform_component.hpp
├── core/
│   ├── engine.hpp
│   ├── plugin.hpp
│   ├── default_plugins.hpp
│   ├── scene.hpp
│   ├── types.hpp
│   ├── window.hpp
│   ├── debug_ui.hpp
│   ├── raw_input_state.hpp
│   └── (utils/)
├── ecs/
│   ├── archetype.hpp
│   ├── archetype_manager.hpp
│   ├── component_registry.hpp
│   ├── entity.hpp
│   ├── entity_manager.hpp
│   ├── query.hpp
│   ├── system.hpp
│   ├── system_manager.hpp
│   ├── thread_pool.hpp
│   └── world.hpp
├── events/
│   ├── collision_event.hpp
│   ├── entity_event.hpp
│   └── event_bus.hpp
├── physics/
│   ├── collision_detection.hpp
│   └── physics_system.hpp
├── renderer/
│   ├── api/irender_device.hpp
│   ├── asset_manager.hpp
│   ├── batch_map.hpp
│   ├── mesh.hpp
│   ├── render_system.hpp
│   ├── shader.hpp
│   ├── texture.hpp
│   └── opengl/
│       ├── gl_buffer.hpp
│       ├── gl_mesh.hpp
│       ├── gl_render_device.hpp
│       ├── gl_shader.hpp
│       └── gl_texture.hpp
├── resources/
│   ├── action_state.hpp
│   ├── input_state.hpp
│   ├── main_camera.hpp
│   └── resource_manager.hpp
└── systems/
    ├── camera_system.hpp
    └── input_system.hpp
```

---

## C++ Techniques Demonstrated

| Technique | Where Used |
|-----------|-----------|
| **Variadic Templates** | `World::registerComponents<Ts...>()`, `Query<Req...>`, `World::addComponents<Ts...>()` |
| **Fold Expressions** | Component registration `(this->registerComponent<Components>(), ...)` |
| **`std::index_sequence`** | Query iteration with compile-time tuple unpacking |
| **`std::unique_ptr`** | RAII management of registry, entity manager, system manager, archetype manager |
| **Placement `new`** | Component construction in pre-allocated archetype memory |
| **`std::type_index`** | Runtime type-to-ID mapping in `ComponentRegistry` |
| **`std::bitset`** | Component signature tracking and archetype matching |
| **Template Metaprogramming** | Signature generation, query type resolution, exclude patterns |
| **Move Semantics** | Component forwarding with `std::forward<T>` |
| **CRTP-like Patterns** | System registration with type name deduplication |
| **Custom Hash Functions** | `DrawKeyHash` for batch grouping in render system |

---

## Test Coverage

| Test Suite | What It Tests |
|------------|--------------|
| `EntityManagerTest` | Entity ID allocation, recycling, record management |
| `ComponentRegistryTest` | Type-to-ID mapping, signature generation, destructor storage for non-trivial types |
| `ArchetypeTest` | Component column storage, entity add/remove, swap-remove behavior, multi-component archetypes |
| `QueryTest` | Required/excluded component matching, iteration over matching archetypes, `exclude()` returning reference |
| `CollisionTest` | AABB-AABB, Sphere-Sphere, AABB-Sphere overlap detection, collision normals, edge cases |
| `WorldTest` | Entity lifecycle, add/remove components, archetype migration, swap-remove record repointing |
| `SerializationTest` | Component round-tripping through JSON, scene save/load |

---

## Future Architecture Considerations

From README.md future plans:
- Custom math library (replace GLM)
- Vulkan renderer backend alongside OpenGL
- Spatial partitioning (BVH, octree) for physics broad-phase
- Serialization system
- Animation system (skeletal)
- Scripting integration (Lua)
- Multi-threaded system execution
- PBR material system
