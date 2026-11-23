# ComponEng User Guide

This guide covers what you need to know to build a game or application using ComponEng.

## Table of Contents

- [Quick Start](#quick-start)
- [Plugins](#plugins)
- [Creating Components](#creating-components)
- [Creating Systems](#creating-systems)
- [Using Queries](#using-queries)
- [Working with Resources](#working-with-resources)
- [Working with Assets](#working-with-assets)
- [Emitting and Handling Events](#emitting-and-handling-events)
- [Networking](#networking)

---

## Quick Start

1. **Create a game plugin**: a class that inherits from `IPlugin`
2. **Register your systems in `build()`**
3. **Create entities and load things in `ready()`**
4. **Add `DefaultPlugins` and your plugin to an `Engine`, then `run()`**

```cpp
#include "componeng/core/default_plugins.hpp"
#include "componeng/core/engine.hpp"

class MyGame : public componeng::core::IPlugin {
public:
    void build(componeng::core::Engine& engine) override {
        // Register systems, resources and components
        engine.world().registerSystem<MySystem>(componeng::ecs::SystemGroup::Simulation);
    }

    void ready(componeng::ecs::World& world) override {
        // Create entities, load assets
    }

    void cleanup(componeng::ecs::World& world) override {
        // Cleanup
    }
};

int main() {
    componeng::core::Engine engine;
    engine.add(componeng::core::DefaultPlugins{}).add(MyGame{}).run();
    return 0;
}
```

---

## Plugins

Everything the engine does comes from a plugin: the window and render device
(`WindowPlugin`), input, physics, the camera, audio and rendering. `DefaultPlugins`
adds all of them. Your game is a plugin too, and so is networking if you use it.
A plugin is a bundle of registrations (components, resources, systems and the
order between them). It has no per-frame hook: per-frame work is a system the
plugin registers.

```cpp
class MyGame : public componeng::core::IPlugin {
public:
    // When added: register things. Cheap; nothing is opened or loaded yet.
    void build(Engine& engine) override;

    // Once every plugin is built, before the first frame: create entities,
    // load assets, open sockets. Systems are created right after.
    void ready(ecs::World& world) override;

    // After the systems are destroyed, in reverse order: save state, clean up.
    void cleanup(ecs::World& world) override;
};
```

**Phases run in the order plugins were added.** `ready` of an earlier plugin runs
before a later one's, so by the time your game's `ready` runs, the window is open,
the built-in assets are loaded and the audio engine is up. `cleanup` runs in the
opposite order, so your game cleans up first.

**What to do in `build()`:**

- Register components with `engine.world().registerComponents<A, B>()`
- Register systems with `engine.world().registerSystem<MySystem>(group)`
- Add resources with `engine.world().setResource(MyResource{...})`
- Order systems with `addSystemDependencies` / `addSystemBefore` (see below)
- Declare a dependency on another plugin with `engine.require<OtherPlugin>()`

**What to do in `ready()`:**

- Create entities with `world.createEntity()`
- Add components with `world.addComponents(entity, component1, component2, ...)`
- Load assets, register sounds, load scenes

### Configuring and replacing the defaults

Plugins are plain objects, so configuration is data on them:

```cpp
core::DefaultPlugins plugins;
plugins.window.title = "My game";
plugins.disable<audio::AudioPlugin>();   // no audio at all
engine.add(plugins).add(MyGame{}).run();
```

A disabled plugin can still be added on its own, or replaced by your own.
Adding the same plugin type twice (say `AudioPlugin` while it is still part of
`DefaultPlugins`) throws and says so. `RenderPlugin` requires `WindowPlugin`; if
you disable the window it builds a default one.

### Writing a plugin for a feature

A reusable feature (networking is one) is a plugin plus its systems:

```cpp
class ScoreboardPlugin : public core::IPlugin {
public:
    int maxPlayers = 8;                       // configuration

    void build(core::Engine& engine) override {
        engine.require<net::NetPlugin>();     // built, and cleaned up after, first
        engine.world().setResource(Scoreboard{maxPlayers});
        engine.world().registerSystem<ScoreboardSystem>(ecs::SystemGroup::Simulation);
    }
};
```

`require<P>()` adds a default `P` if there is none yet. If `P` needs constructor
arguments, add it yourself first, otherwise `require` throws.

### Running without a window

With no `WindowPlugin` the engine has no window and `run()` uses a fixed-rate loop
(60 per second) until a system calls `world.getResource<core::EngineControl>().requestExit()`.
That is what a dedicated server or a test wants. `engine.initialize()`,
`engine.update(dt)` and `engine.shutdown()` step the same thing by hand.

---

## Creating Components

Components are plain old data (POD) structs. They define the data associated with entities.

### Basic Component

```cpp
// file: include/my_components.hpp
#pragma once
#include "componeng/core/types.hpp"  // Provides Vec3, Mat4, etc.

namespace mygame {

struct HealthComponent {
    float current;
    float max;
    bool invulnerable = false;
};

struct VelocityComponent {
    componeng::core::Vec3 linear;
    componeng::core::Vec3 angular;
};

} // namespace mygame
```

### Registering Components

Components must be registered before use. In your plugin's `build()`:

```cpp
void build(core::Engine& engine) override {
    // Register your custom components
    engine.world().registerComponents<
        mygame::HealthComponent,
        mygame::VelocityComponent
    >();
    
    // ... rest of init
}
```

**Note:** Built-in components (TransformComponent, MeshComponent, etc.) are already registered by the engine.

### Adding Components to Entities

```cpp
void ready(ecs::World& world) override {
    ecs::EntityID player = world.createEntity();
    
    // Add single component
    world.addComponent(player, mygame::HealthComponent{100.0f, 100.0f});
    
    // Add multiple components at once (more efficient)
    world.addComponents(player,
        componeng::components::TransformComponent{
            .position = {0.0f, 0.0f, 0.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f}
        },
        mygame::HealthComponent{100.0f, 100.0f},
        mygame::VelocityComponent{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}
    );
}
```

### Accessing Components

```cpp
// Get component (throws if entity doesn't have it)
mygame::HealthComponent& health = world.getComponent<mygame::HealthComponent>(entity);

// Check if entity has component
if (world.hasComponent<mygame::HealthComponent>(entity)) {
    // ...
}

// Remove component
world.removeComponent<mygame::HealthComponent>(entity);
```

---

## Creating Systems

Systems contain the logic that operates on entities. They inherit from `ecs::ISystem`.

### Basic System

```cpp
// file: include/my_systems.hpp
#pragma once
#include "componeng/ecs/system.hpp"

class HealthSystem : public componeng::ecs::ISystem {
public:
    void onUpdate(const componeng::ecs::SystemState& state) override;
};
```

```cpp
// file: src/my_systems.cpp
#include "my_systems.hpp"
#include "my_components.hpp"
#include "componeng/ecs/world.hpp"

void HealthSystem::onUpdate(const componeng::ecs::SystemState& state) {
    // Query all entities with HealthComponent (we modify it, so write<>)
    state.world->query().write<mygame::HealthComponent>().each(
        [](mygame::HealthComponent& health) {
            if (health.current <= 0.0f) {
                health.current = 0.0f;
                health.invulnerable = true;
            }
        }
    );
}
```

### System Lifecycle

Systems have three lifecycle methods:

```cpp
class MySystem : public componeng::ecs::ISystem {
public:
    // Called once when systems are created (before first frame)
    void onCreate(const componeng::ecs::SystemState& state) override {
        // Initialize system resources, cache data, etc.
    }

    // Called every frame
    void onUpdate(const componeng::ecs::SystemState& state) override {
        // Main logic here
    }

    // Called once when systems are destroyed (during shutdown)
    void onDestroy(const componeng::ecs::SystemState& state) override {
        // Cleanup
    }
};
```

### System Groups

Systems are executed in four groups in this order:

| Group | When | Common Systems |
|-------|------|----------------|
| `SystemGroup::PreUpdate` | First | Input processing, networking |
| `SystemGroup::Simulation` | Second | Physics, AI, game logic |
| `SystemGroup::Presentation` | Third | Culling, batching, rendering |
| `SystemGroup::FrameEnd` | Last | Rolling the frame over (input buffers) |

A system never runs before one in an earlier group. Inside a group, systems run in
registration order unless you constrain them:

```cpp
world.addSystemDependencies<MySystem, PhysicsSystem>(); // MySystem runs after PhysicsSystem
world.addSystemBefore<MySystem, CameraSystem>();        // MySystem runs before CameraSystem
```

Constraints are resolved when the systems are created, so they can be added in any
order and before the other system is registered. An unknown system, a constraint
that points into a later group, or a cycle throws at that point instead of being
ignored. Systems are destroyed in reverse of the order they were created.

### Registering Systems

In your plugin's `build()`:

```cpp
void build(core::Engine& engine) override {
    // Register your system with a group
    engine.world().registerSystem<mygame::HealthSystem>(ecs::SystemGroup::Simulation);
    engine.world().registerSystem<mygame::DamageSystem>(ecs::SystemGroup::Simulation);
}
```

### Accessing World and DeltaTime

The `SystemState` gives you access to the world and frame timing:

```cpp
void MySystem::onUpdate(const ecs::SystemState& state) {
    // Access world for queries, resources, etc.
    ecs::World* world = state.world;
    
    // Get frame delta time in seconds
    float dt = state.deltaTime;
    
    // Use in calculations
    float velocity = 100.0f * dt;
}
```

---

## Using Queries

Queries let you iterate over entities that have a specific set of components.
Every component in a query is declared either `read<>` (you only look at it;
the callback gets a `const` reference) or `write<>` (you may modify it; the
callback gets a mutable reference). Writing marks the component's storage as
changed (it bumps the column's version number); reading does not, so prefer
`read<>` whenever you do not modify the data.

### Basic Query

```cpp
// Read-only: the callback receives a const reference
world.query().read<mygame::HealthComponent>().each(
    [](const mygame::HealthComponent& health) {
        // Inspect each entity
    }
);

// Mutable: the callback receives a mutable reference
world.query().write<mygame::HealthComponent>().each(
    [](mygame::HealthComponent& health) {
        health.current += 1.0f;
    }
);
```

### Query with Multiple Components

```cpp
// Query entities with BOTH TransformComponent AND HealthComponent
world.query()
    .read<componeng::components::TransformComponent>()
    .write<mygame::HealthComponent>()
    .each([](const componeng::components::TransformComponent& transform,
              mygame::HealthComponent& health) {
        // Process entities with both components
    });
```

Callback arguments follow the order you declare the components in. `read<>` and
`write<>` each accept several types (`.read<A, B>()`) and can be chained in any
order. A component can only be declared once per query; if you need to both read
and modify it, declare it `write<>`.

### Query with Entity ID

If you need the entity ID (e.g., for adding/removing components):

```cpp
world.query()
    .read<componeng::components::TransformComponent, mygame::HealthComponent>()
    .eachWithEntity([](ecs::EntityID entity,
                        const componeng::components::TransformComponent& transform,
                        const mygame::HealthComponent& health) {
        if (health.current <= 0) {
            // Can now destroy entity if needed
        }
    });
```

### Excluding Components

Exclude entities that have certain components:

```cpp
// Query all entities with TransformComponent EXCEPT those with ColliderComponent
world.query()
    .write<componeng::components::TransformComponent>()
    .exclude<componeng::components::ColliderComponent>()
    .each([](componeng::components::TransformComponent& transform) {
        // ...
    });
```

### Parallel Execution

For large numbers of entities, use `eachParallel` to distribute work across threads:

```cpp
world.query()
    .write<mygame::HealthComponent>()
    .eachParallel(state.world->threadPool(),
        [](mygame::HealthComponent& health) {
            // Processed in parallel
        });
```

**Note:** Avoid modifying components that other systems read simultaneously. If systems need to write to the same data, synchronize carefully or process serially.

---

## Working with Resources

Resources are singleton or shared data accessible by any system. They're stored in the World and accessed by type.

### Built-in Resources

| Resource | Purpose |
|----------|---------|
| `resources::InputState` | Keyboard/mouse input state |
| `resources::ActionState` | Mapped action states (WASD, etc.) |
| `resources::MainCamera` | Entity ID of the main camera |

### Accessing Resources

```cpp
void MySystem::onUpdate(const ecs::SystemState& state) {
    // Get input state
    componeng::resources::InputState& input = 
        state.world->get_resource<componeng::resources::InputState>();
    
    // Check keys
    if (input.isKeyDown(GLFW_KEY_W)) {
        // W is pressed
    }
    
    // Get mouse delta
    float mouseX = input.getMouseDeltaX();
    float mouseY = input.getMouseDeltaY();
}
```

### Using ActionState

```cpp
void MySystem::onUpdate(const ecs::SystemState& state) {
    auto& actions = state.world->get_resource<componeng::resources::ActionState>();
    
    // Check mapped actions
    if (actions.down(componeng::resources::Action::MoveForward)) {
        // Move forward
    }
    
    if (actions.pressed(componeng::resources::Action::Jump)) {
        // Jump just pressed this frame
    }
}
```

### Setting Resources

In your plugin's `build()` (or `ready()`):

```cpp
void ready(ecs::World& world) override {
    // Set the main camera entity
    ecs::EntityID camera = world.createEntity();
    world.addComponent(camera, componeng::components::TransformComponent{});
    world.addComponent(camera, componeng::components::CameraComponent{});
    
    world.set_resource(componeng::resources::MainCamera{camera});
}
```

---

## Working with Assets

The AssetManager provides static methods to load and access shaders, textures, and meshes.

### Loading Assets

Assets are loaded on first access and cached. Load them early, in your plugin's `ready()` (the asset manager exists by then):

```cpp
void ready(ecs::World& world) override {
    using namespace componeng::renderer;
    
    // Load shaders (vertex, fragment, optional geometry)
    AssetManager::loadShader(
        "assets/shaders/my_shader.vert",
        "assets/shaders/my_shader.frag",
        nullptr,
        "my_shader"
    );
    
    // Load texture (alpha = true for RGBA, false for RGB)
    AssetManager::loadTexture("assets/textures/player.png", true, "player");
    
    // Load mesh (wavefront .obj)
    AssetManager::loadMesh("assets/models/player.obj", "player");
}
```

### Accessing Assets

```cpp
using namespace componeng::renderer;

// Get the AssetManager from the world
auto& assetManager = world.getResource<AssetManager>();

// Get asset IDs by name
core::HandleID shaderID = assetManager.getShaderID("my_shader");
core::HandleID textureID = assetManager.getTextureID("player");
core::HandleID meshID = assetManager.getMeshID("player");

// Get const references to use
const Shader& shader = assetManager.getShader(shaderID);
const Texture2D& texture = assetManager.getTexture(textureID);
const Mesh& mesh = assetManager.getMesh(meshID);
```

### Using Assets with Components

```cpp
void ready(ecs::World& world) override {
    using namespace componeng;
    using namespace componeng::components;
    using namespace componeng::renderer;
    
    ecs::EntityID entity = world.createEntity();
    world.addComponents(entity,
        TransformComponent{
            .position = {0.0f, 0.0f, 0.0f},
            .rotation = {0.0f, 0.0f, 0.0f},
            .scale = {1.0f, 1.0f, 1.0f}
        },
        MeshComponent{
            .meshID = AssetManager::getMeshID("player"),
            .visible = true
        },
        MaterialComponent{
            .color = {1.0f, 1.0f, 1.0f},
            .textureID = AssetManager::getTextureID("player"),
            .shaderID = AssetManager::getShaderID("my_shader")
        }
    );
}
```

### Assets outside the engine tree

`Utils::getAssetPath("assets/...")` resolves against the engine's own source
tree. A game that keeps its assets elsewhere registers its root once, before
`Engine::run()` (or `initialize()`); roots added this way are searched first (newest first), then
the engine's:

```cpp
componeng::utils::Utils::addAssetRoot("/path/to/mygame");   // contains assets/...
std::string shader = componeng::utils::Utils::getAssetPath("assets/shaders/my.vert");
```

### Level of detail

Give an entity a `renderer::LodComponent` next to its `MeshComponent`. The
`LodSystem` (registered by the engine) picks the first level whose mesh exists
and whose range covers the entity's distance to the camera, and hides it beyond
the last:

```cpp
renderer::LodComponent lod;
lod.mesh[0] = assets.getMeshID("tree_lod0");  lod.distance[0] = 40.0f;
lod.mesh[1] = assets.getMeshID("tree_lod1");  lod.distance[1] = 120.0f;
// beyond 120 m the entity is hidden
world.addComponents(e, /* transform, MeshComponent, MaterialComponent, */ std::move(lod));
```

A `mesh[k]` of 0 means "no mesh at this level", so a part can exist only up
close. Entities that form one object switch together if they share a position
and ranges.

To choose levels by how much of the screen an entity covers instead of by
metres, set `lod.size` (the object's extent in model space) and a
`screenHeight[k]` threshold per level. The entity's height on screen as a
fraction of the screen's is `size * max(scale) / (2 * distance * tan(fov / 2))`,
level `k` holds while it is at least `screenHeight[k]`, and a threshold of 0
never gives up:

```cpp
renderer::LodComponent lod;
lod.size = 6.0f;                                           // a 6 m tall tree
lod.mesh[0] = assets.getMeshID("tree_lod0");
lod.screenHeight[0] = renderer::lodScreenHeight(6.0f, 40.0f, 70.0f);   // until 40 m
lod.mesh[1] = assets.getMeshID("tree_lod1");
lod.screenHeight[1] = 0.0f;                                // never hidden
```

Unlike metres, this lets big objects keep their detail further away than small
ones and follows the camera's field of view, and it does not depend on the
window's resolution. `lodScreenHeight(worldSize, distance, fov)` gives the
threshold for a switch at a distance you have in mind.

#### Cross-fading between levels

By default a level change dissolves instead of popping. During the transition
the entity draws both meshes, each through a complementary screen-door (dither)
pattern, so every pixel shows exactly one of them: no blending, no sorting, one
extra instance for a fraction of a second. Two `RenderSettings` fields control
it:

```cpp
auto &settings = world.getResource<resources::RenderSettings>();
settings.lodDither = false;        // back to the hard switch
settings.lodFadeSeconds = 0.5f;    // default 0.35; 0 also means instant
```

Turning `lodDither` off mid-transition finishes every transition at once. A
level change back to the level being faded out reverses the fade where it
stands; a change to a third level mid-fade drops the old mesh at once and fades
from the level that was coming in.

The bundled `diffuse` and `pbr` shaders can fade. A shader of your own can
too, by declaring the attribute after its other per-instance ones, passing it
to the fragment stage, and discarding on it (copy `lodDither()` and the first
lines of `main` from `assets/shaders/diffuse.frag`, including the
`COMPONENG_DEPTH_PREPASS` early return, since a batch with fading instances
runs the fragment stage in the depth pre-pass so both passes discard the same
pixels):

```glsl
layout(location = 8) in float instanceFade;   // vertex: 0 = no fade
flat out float LodFade;                        // ... LodFade = instanceFade;
```

A material whose shader has no `instanceFade` attribute keeps the hard switch.
Entities with a `ProceduralInstancesComponent` are not faded. Note that these
shaders now contain a `discard`: with the depth pre-pass off, that can cost
early-Z on some GPUs.

### Textures and buffers from memory

Textures need not come from image files. `createTexture` takes pixels you
computed, in any `api::TextureFormat`: `R8`, `RG8`, `RGB8`, `RGBA8` (bytes),
and `R16F`, `RG16F`, `RGBA16F`, `R32F`, `RG32F`, `RGBA32F` (supplied as `float`,
stored as half or full floats). Float textures are how a shader receives real
data such as heights or densities rather than colour:

```cpp
std::vector<float> heights = bakeHeights();               // 512 x 512
renderer::api::TextureDesc desc;
desc.mipmaps = false;                                     // data, not colour
desc.minFilter = desc.magFilter = renderer::api::TextureFilter::Linear;
assets.createTexture("heights", 512, 512, renderer::api::TextureFormat::R32F,
                     heights.data(), desc);

// Later: overwrite a region in place (rejected if it leaves the texture).
assets.updateTexture(assets.getTextureID("heights"), x, y, w, h, patch);
```

`pixels` may be `nullptr` to allocate now and fill later. `desc.srgb` only
applies to `RGB8`/`RGBA8`.

`createBuffer` / `updateBuffer` do the same for raw GPU buffers.

### Giving a material extra textures and buffers

A material can hand its shader any number of textures and buffers by name.
The RenderSystem binds them before each of the material's draws (textures and
texture buffers take units from 5 up; uniform blocks take binding points from 1
up; the per-frame block stays at 0):

```cpp
auto &mat = assets.getMaterial(assets.getMaterialID("grass"));
mat.setTexture("heightMap", assets.getTextureID("heights"));      // sampler2D
mat.setUniformBuffer("Settings", assets.getBufferID("settings")); // std140 block
mat.setBufferTexture("positions", assets.getBufferID("positions"),
                     renderer::api::BufferTextureFormat::RGBA32F); // samplerBuffer
```

```glsl
uniform sampler2D heightMap;
layout(std140) uniform Settings { vec4 tint; };
uniform samplerBuffer positions;                // texelFetch(positions, i)
```

Use a uniform block for a few values (blocks are limited to roughly 16 KB) and
a buffer texture for long lists, which has no such small limit. Setting the
same name again replaces what it points at. These work for ordinary and
procedural draws alike.

### Procedural instancing

For things too numerous to be entities (grass, debris, rain), one entity can
drive a whole instanced draw. Add a `ProceduralInstancesComponent` next to the
usual transform, mesh and material; the engine draws the mesh `count` times and
uploads **no** per-instance data. The vertex shader works out each copy from
`gl_InstanceID`, the per-frame block (camera, time, light) and material
uniforms:

```cpp
world.addComponents(field,
    core::TransformComponent{},
    renderer::MeshComponent{.meshName = "blade"},
    renderer::MaterialComponent{.materialName = "grass"},
    renderer::ProceduralInstancesComponent{.count = 200000});
```

```glsl
// grass.vert: no per-instance attributes, so the material has none either
float h1 = hash(float(gl_InstanceID));
vec2 cell = vec2(gl_InstanceID % 512, gl_InstanceID / 512) * 0.3 + h1;
vec3 root = vec3(cell.x, 0.0, cell.y);   // placed entirely in the shader
```

Things to know:

- The shader must not declare per-instance attributes (`instanceModel`, ...);
  there is no instance buffer, so they would read zero. The engine logs an
  error if it finds some.
- Each such entity is its own draw call, never merged with others that share a
  mesh and material.
- The entity's transform is used only for culling, not passed to the shader.
- Culling is **off** by default, since shader-placed instances can land outside
  the mesh's bounds. Set `.cull = true` for a static patch whose instances stay
  inside the mesh bounds (plus the material's bounds padding).

---

## Emitting and Handling Events

The event system allows loose coupling between systems.

### Defining Events

```cpp
// file: include/my_events.hpp
#pragma once
#include "componeng/ecs/entity.hpp"

namespace mygame {

struct DamageEvent {
    componeng::ecs::EntityID target;
    componeng::ecs::EntityID attacker;
    float amount;
};

struct DeathEvent {
    componeng::ecs::EntityID entity;
};

} // namespace mygame
```

### Emitting Events

```cpp
void MySystem::onUpdate(const ecs::SystemState& state) {
    // Emit an event
    state.world->emit_event<mygame::DamageEvent>({
        .target = targetEntity,
        .attacker = attackerEntity,
        .amount = 10.0f
    });
}
```

### Handling Events

Events are stored double-buffered. Read them in a system:

```cpp
void DamageSystem::onUpdate(const ecs::SystemState& state) {
    // Get all DamageEvents from this frame
    const auto& events = state.world->get_events<mygame::DamageEvent>();
    
    for (const auto& event : events) {
        // Process each event
        auto& health = state.world->getComponent<mygame::HealthComponent>(event.target);
        health.current -= event.amount;
        
        if (health.current <= 0) {
            // Emit death event
            state.world->emit_event<mygame::DeathEvent>({event.target});
        }
    }
}
```

---

## Networking

The engine has a peer-to-peer networking layer: no server is required, any
instance can connect to any other, and a game works with typed messages rather
than sockets or bytes.

### Turning it on

Networking is optional and lives in its own library, `ComponEngNet`. A game that
does not use it contains none of its code. To opt in:

```cmake
target_link_libraries(MyGame PRIVATE ComponEngNet)   # brings ComponEng along
```

```cpp
#include "componeng/net/net_plugin.hpp"

engine.add(DefaultPlugins{})
      .add(componeng::net::NetPlugin{})   // the resources and the system, once
      .add(MyGame{});
```

`NetPlugin` creates the `Network`, `Session`, `LanBrowser` and `EcsBridge`
resources and registers a system that updates them each frame ahead of the
systems that read the results. On cleanup it leaves the session and closes the
network. No socket is opened until you call `Network::open`. The build option
`COMPONENG_NETWORKING` (default ON) controls whether `ComponEngNet` and its tests
are built at all.

### Defining messages

A message is a struct with a unique `kName` and one `serialize` function, which
is used for both writing and reading so the two cannot disagree:

```cpp
#include "componeng/net/network.hpp"

struct MoveMessage {
  static constexpr const char *kName = "chess.move";
  uint8_t from = 0, to = 0, promotion = 0;
  template <typename Ar> void serialize(Ar &ar) { ar(from, to, promotion); }
};
```

Supported field types: `bool`, integers, enums, `float`/`double`, `std::string`,
`std::vector`, `std::array`, other serializable structs, and the engine's `Vec2`,
`Vec3`, `Vec4` (include `componeng/net/archive_math.hpp`). Use fixed-width
integers (`uint32_t`, not `unsigned long` or `size_t`) so peers on different
platforms agree. Data from the network is untrusted: reads are bounds-checked
and a malformed message is dropped, but checking that an enum or float holds a
sensible value is up to the game.

### Opening, connecting, sending

```cpp
auto &net = world.getResource<componeng::net::Network>();

net.registerMessage<MoveMessage>();                 // reliable, in order
net.registerMessage<CursorMessage>(net::Channel::Unreliable); // may be lost
net::TransportConfig config;
config.protocolId = 0x4D594741;      // required: a number of your game's own
net.open(7777, config);              // port 0 picks a free one
net.setAcceptIncoming(true);         // on the peer that others join

net.connect("192.168.1.20:7777");    // on the peer that joins (numeric IP)
net.send(peer, MoveMessage{12, 28, 0});
net.broadcast(MoveMessage{12, 28, 0});             // every connected peer
```

`protocolId` identifies your game (and its protocol version): peers with another
value ignore each other, so two games on one network never mix. `open` refuses
to run without one. Every peer registers the same message types. Use `Channel::Reliable` for
anything that must arrive exactly once and in order (a chess move), and
`Channel::Unreliable` for what is stale a moment later (a cursor position).

### Receiving

As callbacks (run during the network update), as ECS events, or both:

```cpp
net.onMessage<MoveMessage>([](const net::Address &from, const MoveMessage &m) { ... });
net.onPeerConnected([](const net::Address &peer) { ... });

// ECS events are opt in per message type, through the EcsBridge:
world.getResource<net::EcsBridge>().forward<MoveMessage>(net);
// then, in a system, readable the frame after they arrive:
for (const auto &e : world.getEvents<events::MessageReceivedEvent<MoveMessage>>())
  use(e.from, e.message);
// Peer events (PeerConnectedEvent, PeerDisconnectedEvent, ConnectFailedEvent) and
// session events need no opt-in once NetPlugin has been added
// (events/network_event.hpp, events/session_event.hpp).
```

`NetPlugin`'s system runs the network every frame. The networking core does not depend
on the ECS, so it can also be used without `NetPlugin`: construct a
`Network` yourself and call `net.update(now)` with seconds from a steady clock.

### Sessions: more than two peers

`Network` connects peers one pair at a time. For a game with several players,
use a `Session`: one peer hosts, others join, and every peer ends up connected
to every other (a mesh). Peers are named by `PeerId` (the host is 0, then 1, 2,
...), and ids are not reused while the session lives.

```cpp
auto &session = world.getResource<net::Session>();
session.registerMessage<PieceMoved>();      // instead of net.registerMessage
net.open(7777);

session.host({.name = "Jigsaw night"});     // peer 0
// or, on another machine:
session.join("192.168.1.20:7777");

session.onJoined([] { /* in, and connected to everyone */ });
session.onPeerJoined([](net::PeerId id) { ... });
session.onPeerLeft([](net::PeerId id, net::DisconnectReason why) { ... });

session.broadcast(PieceMoved{...});         // to every other peer
session.send(2, PieceMoved{...});           // to peer 2
session.onMessage<PieceMoved>([](net::PeerId from, const PieceMoved &m) { ... });
```

The **leader** is the one peer that admits joiners and answers LAN searches:
the host, and if it leaves, the peer with the lowest id. Everything else is
symmetric; a game that wants an authority (for example to settle conflicting
moves) can use the leader as one. ECS events: `SessionJoinedEvent`,
`SessionJoinFailedEvent`, `SessionPeerJoinedEvent`, `SessionPeerLeftEvent`,
`SessionLeaderChangedEvent` (all emitted once `NetPlugin` has been added) and
`SessionMessageEvent<T>`, which is opt in per type with
`bridge.forward<T>(session)` (`events/session_event.hpp`).

### Shared objects: one owner at a time

For state every peer sees and edits, such as the pieces of a shared jigsaw, use
`SharedObjects<T>`. Each object is either free or owned by exactly one peer, and
only its owner may change it, so two players cannot drag the same piece. The
session's leader decides who gets an object when several ask at once.

```cpp
struct Piece {
  float x = 0, y = 0;
  bool placed = false;
  template <typename Ar> void serialize(Ar &ar) { ar(x, y, placed); }
};
net::SharedObjects<Piece> pieces(session, "jigsaw.pieces"); // same name on every peer

pieces.add(pieces.newId(), Piece{...});        // e.g. the host, once, at the start

pieces.claim(id);                              // on mouse down: ask for it
pieces.onOwnerChanged([&](net::ObjectId id, net::PeerId owner) { ... });
pieces.onClaimDenied([&](net::ObjectId id, net::PeerId owner) { ... });

if (pieces.ownedByMe(id)) pieces.update(id, Piece{x, y, false}); // each frame while dragging
pieces.release(id, Piece{x, y, snapped});      // on mouse up: final state, object is free

if (const Piece *p = pieces.get(id)) draw(*p); // every frame, on every peer
```

The claim is a request: the game starts dragging when `onOwnerChanged` says it
owns the piece (or when `ownedByMe` turns true), and gives up if it is denied.
`update` streams unreliably, so a lost update is replaced by the next one;
`release` delivers the final state reliably, so a piece always ends where its
owner dropped it. A peer that leaves frees what it owned. A peer that joins is
sent the whole set (`onSynced` fires when it has arrived), and the set is
empty and `synced()` is false until then. State sent with `update` must fit one
packet (about 1.1 KB); `add` and `release` can be larger.

Callbacks report what other peers did, and the leader's decisions about this
peer's claims. Ownership operations need an active session; `add`, `remove` and
`get` also work on a set used alone. Peers are trusted to play by the rules:
this keeps friends in step, it does not stop a modified client from cheating.

### Finding games on the local network

A hosting session answers LAN searches by default (`SessionConfig::advertiseOnLan`).
To list them, use the world's `LanBrowser`:

```cpp
auto &browser = world.getResource<net::LanBrowser>();
browser.start(net.config().protocolId);     // same value on every peer of a game
// each frame, until the player picks one:
for (const net::LanSessionInfo &s : browser.sessions())
  show(s.name, s.peers, s.maxPeers);
session.join(chosen);                       // a LanSessionInfo
```

Searching asks the broadcast address once a second; nothing is sent while idle.
If two hosts on one machine use the same discovery port, only one of them hears
a given search.

### Playing over the internet

Behind a home router a peer's own address is no use to anyone outside, and the
router drops packets it did not expect. The engine gets through with STUN and
hole punching, and no server of your own:

1. **Each peer asks a public STUN server what its address looks like from
   outside.** The defaults are Google's and Cloudflare's public servers; they
   are third-party services with no guarantee, and they see your IP.
2. **The peers swap those addresses themselves**, as a short *invite code*
   (`0K10-R218-2E45-0`) read out, pasted into a chat, or sent any way you like.
   STUN cannot introduce peers to each other; this step is yours (or your
   game's lobby).
3. **Both send to each other at about the same moment**, which makes each router
   let the other's packets in.

Internet play is a second opt-in on top of networking: link `ComponEngNetNat` and
add `NatPlugin` (it requires `NetPlugin`), which creates a `NatTraversal` for the
`Network` before it opens, as it has to (it wraps the socket). A game that only
plays on a LAN leaves it out.

```cpp
auto &net = world.getResource<net::Network>();
auto &nat = world.getResource<net::NatTraversal>();   // from NatPlugin
net.open(0, config);
nat.onDiscovery([&](bool ok, const net::NatInfo &info) {
  if (ok) show(nat.inviteCode());           // "0K10-R218-2E45-0": give this to the other player
});
nat.discoverPublicAddress();                // a second or two

// The player being joined, once they have the joiner's code:
session.expectJoiner(addressFromCode);      // or net.punch(address) without a session
// The joiner, with the host's code:
net::Address host;
if (net::decodeInvite(codeTypedByPlayer, host))
  session.join(host);
```

`nat.connectToInvite(code)` does the decode and `connect` for a plain `Network`.
Peers that join later need no codes: the session's leader introduces them to
each other. Raise `TransportConfig::connectTimeout` (default 5 s) if players
exchange codes slowly.

**It works through most home routers, and not through all.** A router that
gives each destination a different public port (a *symmetric* NAT, common on
mobile data, some campus and company networks) cannot be punched through, as the
address STUN reports is only valid toward the STUN server. `nat.natType()` says
which you have (`Cone` is fine, `Symmetric` is not), once discovery is done.
A symmetric peer can still *connect to* a host that is reachable without
punching (a public IP, a forwarded port, or a "full cone" router). For the rest only a relay (TURN) helps, which the engine does not provide.
Peers on the same home network as each other, reaching each other through the
router's public address, may also fail ("hairpinning"); use the LAN address
there.

### Scope

LAN play and direct-IP play work, as does internet play through ordinary home
routers, for up to a handful of peers (every peer keeps a connection to every
other, so keep `maxPeers` a few under `TransportConfig::maxConnections`). Two
peers that stay connected to the rest but lose contact with each other are not
reconnected.

---

## Complete Example

Here's a simplified version of how the example game works:

```cpp
// main.cpp
#include "componeng/core/engine.hpp"
#include "componeng/core/game.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/components/mesh_component.hpp"
#include "componeng/components/material_component.hpp"
#include "componeng/components/rigidbody_component.hpp"
#include "componeng/components/collider_component.hpp"
#include "componeng/renderer/asset_manager.hpp"

#include "player_controller.hpp"  // Custom system

class Game : public componeng::core::IPlugin {
public:
    void build(componeng::core::Engine& engine) override {
        engine.world().registerSystem<PlayerController>(componeng::ecs::SystemGroup::Simulation);
    }

    void ready(componeng::ecs::World& world) override {
        // Create 100 cubes with physics
        for (int i = 0; i < 100; ++i) {
            componeng::ecs::EntityID entity = world.createEntity();
            
            float x = static_cast<float>(i % 10) * 2.0f;
            float z = static_cast<float>(i / 10) * 2.0f;
            
            world.addComponents(entity,
                componeng::components::TransformComponent{
                    .position = {x, 10.0f, z},
                    .rotation = {0.0f, 0.0f, 0.0f},
                    .scale = {1.0f, 1.0f, 1.0f}
                },
                componeng::components::MeshComponent{
                    .meshID = componeng::renderer::AssetManager::getMeshID("cube"),
                    .visible = true
                },
                componeng::components::MaterialComponent{
                    .color = {1.0f, 0.5f, 0.2f, 1.0f},
                    .textureID = componeng::renderer::AssetManager::getTextureID("white"),
                    .shaderID = componeng::renderer::AssetManager::getShaderID("default")
                },
                componeng::components::RigidBodyComponent{
                    .type = componeng::components::RigidBodyComponent::Dynamic,
                    .mass = 1.0f,
                    .restitution = 0.5f
                },
                componeng::components::ColliderComponent{
                    .type = componeng::components::ColliderType::Box,
                    .transform = {
                        .position = {0.0f, 0.0f, 0.0f},
                        .rotation = {0.0f, 0.0f, 0.0f},
                        .scale = {1.0f, 1.0f, 1.0f}
                    }
                }
            );
        }
    }

    void cleanup(componeng::ecs::World& world) override {
        // Cleanup if needed
    }
};

int main() {
    componeng::core::Engine engine;
    engine.add(componeng::core::DefaultPlugins{}).add(Game{}).run();
    return 0;
}
```

---

## Common Patterns

### Adding/Removing Components at Runtime

```cpp
void PowerUpSystem::onUpdate(const ecs::SystemState& state) {
    state.world->query().write<mygame::PowerUpComponent>().each(
        [state](mygame::PowerUpComponent& powerUp) {
            powerUp.duration -= state.deltaTime;
            if (powerUp.duration <= 0) {
                // Remove component - entity moves to different archetype
                state.world->removeComponent<mygame::PowerUpComponent>(/* entity */);
            }
        }
    );
}
```

### Creating Entities from Templates

```cpp
ecs::EntityID createEnemy(ecs::World& world, const Vec3& position) {
    ecs::EntityID enemy = world.createEntity();
    world.addComponents(enemy,
        TransformComponent{.position = position},
        MeshComponent{.meshID = AssetManager::getMeshID("enemy")},
        HealthComponent{50.0f, 50.0f},
        AIComponent{}
    );
    return enemy;
}
```

### Frame-Rate Independent Movement

```cpp
void MovementSystem::onUpdate(const ecs::SystemState& state) {
    state.world->query().write<TransformComponent>().read<VelocityComponent>().each(
        [state](TransformComponent& transform, const VelocityComponent& velocity) {
            transform.position += velocity.linear * state.deltaTime;
        }
    );
}
```