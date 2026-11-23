# ComponEng — Game Development Guide

This is a hands-on guide to building a game with ComponEng, a C++17 ECS-based engine
using OpenGL 3.3. It walks through the full pipeline: project setup, the game entry
point, components, systems, queries, resources, assets, physics, audio, events, and
a complete worked example. It ends with a section on what the engine would need to
render a high-quality forest scene (grass, lighting, etc.).

> Existing reference docs: `docs/USER.md` (API reference) and `docs/DESIGN.md`
> (architecture). This guide is task-oriented: "how do I make a game?"

---

## 1. Project setup

ComponEng is a CMake project. Most third-party dependencies are vendored under
`include/third_party` and `src/third_party`. You only need GLM, GLFW, and OpenGL
installed on your system.

```bash
git clone <repository-url>
cd ComponEng
make build          # builds libComponEng.a + the Demo example + tests
./build/example/Demo
```

Controls: WASD + Space/Shift to move, mouse to look, 0 to toggle cursor lock,
Escape to quit.

To make your own game, create a new executable target (or add to the example
folder) that links against `ComponEng` and includes the engine headers:

```cmake
add_executable(MyGame src/main.cpp src/my_systems.cpp)
target_include_directories(MyGame PRIVATE include)
target_link_libraries(MyGame PRIVATE ComponEng)
```

---

## 2. The game entry point (a plugin)

Your game is a plugin: a class inheriting from `componeng::core::IPlugin`. The
engine is built from plugins (window, input, physics, camera, audio, rendering;
`DefaultPlugins` adds them all) and you add yours on top.

```cpp
#include "componeng/core/default_plugins.hpp"
#include "componeng/core/engine.hpp"

class MyGame : public componeng::core::IPlugin {
public:
    // When added: register components, systems and resources.
    void build(componeng::core::Engine& engine) override {
        // 1. Register custom components
        // 2. Register custom systems
        // 3. Set resources
    }

    // Before the first frame, once every plugin is ready (window open,
    // built-in assets loaded): create the starting state.
    void ready(componeng::ecs::World& world) override {
        // 4. Create entities + add components
        // 5. Load assets (shaders, textures, meshes, materials)
    }

    // After systems are destroyed, before the window closes.
    void cleanup(componeng::ecs::World& world) override {
        // Optional: save scene, write telemetry, etc.
    }
};

int main() {
    componeng::core::Engine engine;
    engine.add(componeng::core::DefaultPlugins{}).add(MyGame{}).run();
    return 0;
}
```

`Engine::run()` does: every plugin's `ready()` in the order added →
`world.createSystems()` → the runner (the window's main loop: system updates,
ImGui, swap buffers) → `world.destroySystems()` → every plugin's `cleanup()` in
reverse order. A plugin's `build()` runs as soon as it is added.

Plugins are also how features are packaged. `plugins.disable<audio::AudioPlugin>()`
drops one from the defaults, `engine.require<OtherPlugin>()` inside `build()`
declares a dependency, and without `WindowPlugin` the engine runs headless.
See the User Guide's Plugins section.

---

## 3. Creating components

Components are plain data structs. Put them in a header, register them in your plugin's `build()`,
and add them to entities.

### Define a component

```cpp
#pragma once
#include "componeng/core/types.hpp"

namespace mygame {

struct HealthComponent {
    float current = 100.0f;
    float max     = 100.0f;
    bool  invulnerable = false;
};

struct VelocityComponent {
    componeng::core::Vec3 linear  = {0,0,0};
    componeng::core::Vec3 angular = {0,0,0};
};

} // namespace mygame
```

### Register components

Built-in components (Transform, Mesh, Material, Color, Camera, RigidBody, Collider,
Audio, DirectionalLight, PointLight) are registered by the engine automatically.
Register your own in `build()`:

```cpp
world.registerComponents<
    mygame::HealthComponent,
    mygame::VelocityComponent
>();
```

> Limit: `MAX_COMPONENTS = 32` distinct component types, `MAX_ENTITIES = 10000`,
> `MAX_ARCHETYPES = 64`. Keep this in mind for large worlds.

### Add components to an entity

```cpp
ecs::EntityID player = world.createEntity();

world.addComponents(player,
    componeng::components::TransformComponent{
        .position = {0.0f, 2.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f},
        .scale    = {1.0f, 1.0f, 1.0f}
    },
    mygame::HealthComponent{100.0f, 100.0f},
    mygame::VelocityComponent{{5.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}}
);
```

Adding multiple components in one `addComponents` call is more efficient because the
entity moves to its final archetype in a single step.

### Access components

```cpp
auto& health = world.getComponent<mygame::HealthComponent>(player);
if (world.hasComponent<mygame::VelocityComponent>(player)) {
    auto& vel = world.getComponent<mygame::VelocityComponent>(player);
}
world.removeComponent<mygame::HealthComponent>(player);
```

Removing a component moves the entity to a different archetype (swap-remove, O(1)).

---

## 4. Creating systems

Systems contain game logic and run on entities matching a query. Inherit from
`componeng::ecs::ISystem`.

```cpp
#pragma once
#include "componeng/ecs/system.hpp"

class HealthSystem : public componeng::ecs::ISystem {
public:
    void onUpdate(const componeng::ecs::SystemState& state) override;
};
```

```cpp
#include "health_system.hpp"
#include "my_components.hpp"
#include "componeng/ecs/world.hpp"

void HealthSystem::onUpdate(const componeng::ecs::SystemState& state) {
    state.world->query().write<mygame::HealthComponent>().each(
        [](mygame::HealthComponent& health) {
            if (health.current <= 0.0f) health.current = 0.0f;
        }
    );
}
```

### System lifecycle

| Method | When |
|--------|------|
| `onCreate` | Once, before the first frame |
| `onUpdate` | Every frame |
| `onDestroy` | Once, during shutdown |

### System groups & execution order

Systems run in four groups, in this order:

| Group | Typical systems |
|-------|-----------------|
| `PreUpdate` | Input processing, networking |
| `Simulation` | Physics, AI, gameplay logic |
| `Presentation` | Culling, batching, rendering |
| `FrameEnd` | Rolling the frame over (input buffers) |

Register with a group:

```cpp
world.registerSystem<mygame::HealthSystem>(componeng::ecs::SystemGroup::Simulation);
```

You can also declare ordering inside a group, so one system runs after (or before)
another:

```cpp
world.addSystemDependencies<renderer::BatchingSystem, renderer::CullingSystem>(); // after
world.addSystemBefore<MySystem, camera::CameraSystem>();                          // before
```

They are resolved when the systems are created, so order of declaration does not
matter, and an unknown system, a reference to a later group or a cycle throws.

### SystemState

```cpp
float dt = state.deltaTime;           // seconds since last frame
ecs::World* world = state.world;      // queries, resources, events
double time = state.world->getTime(); // total elapsed time
```

---

## 5. Using queries

Queries iterate entities that have a specific set of components. They're the
primary way systems find their data. Declare each component as `read<>` (you
only look at it; you get a `const` reference) or `write<>` (you may change it;
you get a mutable reference and its storage is marked as changed). Callback
arguments follow declaration order.

```cpp
// All entities with a Transform, read-only
world.query().read<componeng::components::TransformComponent>().each(
    [](const componeng::components::TransformComponent& t) { /* ... */ }
);

// Entities with Transform (read) AND Mesh (modified)
world.query()
    .read<componeng::components::TransformComponent>()
    .write<componeng::renderer::MeshComponent>()
    .each([](const auto& transform, auto& mesh) { /* ... */ });

// With entity ID (needed to add/remove/destroy entities)
world.query().read<componeng::components::TransformComponent>()
    .eachWithEntity([](ecs::EntityID e, const auto& transform) {
        if (shouldDie(e)) world.destroyEntity(e);
    });

// Exclude entities with a component
world.query().write<componeng::components::TransformComponent>()
    .exclude<componeng::components::ColliderComponent>()
    .each([](auto& transform) { /* ... */ });

// Parallel over the thread pool
world.query().write<mygame::HealthComponent>()
    .eachParallel(state.world->threadPool(),
        [](mygame::HealthComponent& h) { /* ... */ });
```

A component may be declared only once per query; to both read and modify it,
declare it `write<>`. Outside queries, `world.getComponent<T>(e)` is a mutable
(write) access and `world.readComponent<T>(e)` is the read-only one.

> `eachParallel` is great for large entity counts, but don't write to the same
> component from multiple threads without synchronization.

---

## 6. Working with resources

Resources are singletons shared across systems, stored in the World by type.

### Built-in resources

| Resource | Purpose |
|----------|---------|
| `resources::InputState` | Raw keyboard / mouse state |
| `resources::ActionState` | Mapped actions (MoveForward, Jump, Sprint...) |
| `resources::MainCamera` | Entity ID of the active camera |
| `renderer::AssetManager` | Loaded shaders, textures, meshes, materials |
| `audio::AudioManager` | Sound playback |
| `renderer::api::IRenderDevice` | GPU backend (OpenGL) |

### Accessing resources

```cpp
auto& input  = state.world->getResource<componeng::resources::InputState>();
auto& actions= state.world->getResource<componeng::resources::ActionState>();
auto& cam    = state.world->getResource<componeng::camera::MainCamera>();

if (actions.down(componeng::resources::Action::MoveForward)) { /* ... */ }
float mx = input.getMouseDeltaX();
float my = input.getMouseDeltaY();
```

### Setting resources

```cpp
ecs::EntityID camera = world.createEntity();
world.addComponents(camera,
    componeng::components::TransformComponent{},
    camera::CameraComponent{.fov = 45.0f, .aspectRatio = 16.0f/9.0f,
                            .nearPlane = 0.1f, .farPlane = 10000.0f});
world.setResource(componeng::resources::MainCamera{camera});
```

Or set a custom resource:

```cpp
struct GameSettings { float volume = 0.8f; };
world.setResource(GameSettings{});
auto& settings = world.getResource<GameSettings>();
```

---

## 7. Working with assets

`renderer::AssetManager` loads shaders, textures, meshes, and materials on first
access and caches them by name. All access is static.

Load assets early, in `ready()`:

```cpp
using namespace componeng::renderer;

AssetManager::loadShader(
    "assets/shaders/forest.vert",
    "assets/shaders/forest.frag",
    nullptr, "forest");
AssetManager::loadTexture("assets/textures/grass.png", true, "grass");
AssetManager::loadMesh("assets/models/tree.obj", "tree");

// Register a material (shader + texture pair)
AssetManager::registerMaterial<renderer::DiffuseMaterial>(
    "grass_mat", "forest", "grass");
```

Access by ID:

```cpp
auto& assetManager = world.getResource<renderer::AssetManager>();
core::HandleID meshID   = assetManager.getMeshID("tree");
core::HandleID texID    = assetManager.getTextureID("grass");
core::HandleID shaderID = assetManager.getShaderID("forest");
core::HandleID matID    = assetManager.getMaterialID("grass_mat");
```

### Materials

`MaterialComponent.materialName` refers to a registered material name. At render
time the engine resolves it to a shader + texture + vertex layout.

`DiffuseMaterial` (built-in) writes per-instance data: `instanceModel` (mat4) and
`instanceColor` (vec4). The diffuse shader reads:

- `uniform mat4 viewProj`
- `layout(location = 3) in mat4 instanceModel`
- `layout(location = 7) in vec4 instanceColor`
- `uniform sampler2D Tex`
- `uniform vec3 lightDir`
- `uniform vec3 lightColor`

Write a custom `IMaterial` subclass to add more per-instance attributes or uniforms
(e.g., wind speed for grass, normal maps, PBR parameters). Override
`writeInstanceData`, which runs once per rendered entity per frame and writes
straight into the batch's instance buffer:

```cpp
class GrassMaterial : public renderer::IMaterial {
public:
  using IMaterial::IMaterial;

  // Called when the material receives its vertex layout. Look the shader's
  // attributes up here, once, not per entity.
  void resolveInstanceSlots(const renderer::api::VertexLayout &layout) override {
    m_position = renderer::findInstanceSlot(layout, m_positionName);
    m_wind = renderer::findInstanceSlot(layout, m_windName);
  }

  void writeInstanceData(const renderer::InstanceContext &ctx,
                         renderer::InstanceWriter &out) const override {
    const auto &grass = ctx.world.getComponent<GrassComponent>(ctx.entity);
    out.set(m_position, grass.position);   // matches `in vec3 instancePosition`
    out.set(m_wind, grass.windStrength);   // matches `in float instanceWind`
  }

private:
  const core::Name m_positionName{"instancePosition"};
  const core::Name m_windName{"instanceWind"};
  renderer::InstanceSlot m_position;
  renderer::InstanceSlot m_wind;
};
```

Use `findInstanceMatrixSlot` for a `mat4` attribute. A slot is just the
attribute's offset and width, so writing through one is a plain copy; writing by
`Name` (`out.set(core::Name("instancePosition"), ...)`) still works but searches
the layout on every call, which adds up over thousands of entities.

`InstanceContext` carries the world, the entity, its `TransformComponent` and a
cached `modelMatrix()`. Attribute names must match the shader's instance
attributes; an attribute the shader does not declare gives an invalid slot, and
writing to one does nothing. `writeInstanceData` is the only per-instance hook
a material has, and every material must override it.

#### Per-frame values in custom shaders

Camera, light, fog and time are uploaded once per frame to a uniform buffer. A
shader opts in by declaring this block (identically in its vertex and fragment
stages) and then uses the members by bare name:

```glsl
layout(std140) uniform FrameData {
    mat4 viewProj;
    vec3 cameraPos;      float time;
    vec3 lightDir;       float fogDensity;   // lightDir points towards the light
    vec3 lightColor;     float fogStart;
    vec3 ambientColor;   float ambientStrength;
    vec4 fogColor;
    float fogEnd;
};
```

The layout must match `renderer/frame_data.hpp`; the engine logs an error at
shader load if the sizes disagree. The values are only ever uploaded through
this block: a shader that reads `viewProj`, `time` and the rest as plain
`uniform`s gets zeros, and the engine logs an error at load naming each one.
A shader that needs no per-frame values can leave the block out.

#### Material type and render state

Each material has a `MaterialType` that decides which pass draws it:

- `Opaque` (the default): drawn first, after a depth pre-pass, so each pixel runs
  the fragment shader once. The renderer sets blending and depth for these
  itself; the material's `RenderState` only contributes its culling mode. The
  fragment shader must not `discard`, because the pre-pass cannot see it.
- `Cutout`: opaque, but the fragment shader may `discard` (alpha-tested leaves,
  fences, billboards). Drawn with the opaque batches in the same state. In the
  pre-pass it runs its own fragment shader, so the holes are cut into depth too
  (see below).
- `Transparent`: drawn after the opaque geometry and the sky, with the
  material's `RenderState` applied as given. Use it for anything blended, and
  for overlays that ignore depth (`depthTest = false`).

```cpp
auto &glass = assetManager.getMaterial(assetManager.getMaterialID("glass"));
glass.setType(renderer::MaterialType::Transparent);
```

The depth pre-pass runs each opaque shader's vertex stage again with an empty
fragment shader, so a vertex shader used by an opaque material must declare
`invariant gl_Position;` (the bundled shaders do) to get bit-identical depth in
both passes; without it you may see speckled gaps. Turn the pre-pass off with
`world.getResource<resources::RenderSettings>().depthPrepass = false`.

A `Cutout` material's fragment shader is compiled a second time with
`COMPONENG_DEPTH_PREPASS` defined, for the pre-pass, so there is one copy of the
alpha test. Return right after the `discard` when the macro is set, so the
pre-pass skips the lighting; without that it is still correct, only slower:

```glsl
if (tex.a < cutoff) discard;
#ifdef COMPONENG_DEPTH_PREPASS
return;
#endif
```

The pre-pass binds the material's albedo texture (and what it added with
`setTexture`), not the PBR set, so the alpha test must read only those. Whether
the pre-pass pays off depends on the scene: it repeats the vertex stage of every
opaque and cutout draw, so a vertex-heavy scene can lose more than it saves.

Each material also carries a `RenderState` (culling, depth test/write/function
and blending) that the renderer applies before drawing it. The default is
back-face culling, depth test and write, and alpha blending.

```cpp
renderer::api::RenderState state;
state.cull = renderer::api::CullMode::None;     // double-sided
state.blend = renderer::api::BlendMode::None;   // opaque, no blending cost
assetManager.getMaterial(assetManager.getMaterialID("leaf")).setRenderState(state);
```

#### Texture options

`loadTexture` takes an optional `TextureDesc`. The defaults are the engine's
original behaviour (clamped, trilinear, linear colour), so existing code is
unchanged.

```cpp
renderer::api::TextureDesc desc;
desc.wrap = renderer::api::TextureWrap::Repeat;   // tile instead of clamping
desc.anisotropy = 8.0f;                           // sharper at grazing angles
assets.loadTexture(path, /*alpha=*/false, "ground", desc);
```

| Field | Values | Notes |
|---|---|---|
| `wrap` | `ClampToEdge` (default), `Repeat`, `MirroredRepeat` | applies to both axes |
| `minFilter`, `magFilter` | `Linear` (default), `Nearest` | `Nearest` gives crisp pixels |
| `mipmaps` | `true` (default) | with `false`, minification does not blend levels |
| `anisotropy` | `1` = off (default) | clamped to the GPU's limit, ignored if unsupported |
| `srgb` | `false` (default) | see below |

Set `srgb = true` only for colour images that a shader reads and then lights in
linear space and encodes itself, as `pbr.frag` does. Normal, roughness, metallic
and AO maps are data and stay linear. With a shader that uses the sampled value
directly as the output colour (the diffuse shader), an sRGB texture comes out
too dark. A file that cannot be read becomes a 1x1 magenta texture and an error
is logged.

#### Culling

`CullingSystem` tests every renderable entity's bounding box (the mesh's own
bounds under its transform) against the camera frustum, whether or not it has a
physics collider, and sets `MeshComponent::culled`. Culled entities are skipped
before any instance data is written. `MeshComponent::visible` is yours: the
engine never changes it, so hiding an entity and culling one are independent.

An entity's ids are resolved during its first frame, so it is drawn
unconditionally that frame and culled from the next.

If a vertex shader moves geometry outside the mesh's own bounds (billboards,
wind sway, waves), widen the box so such entities are not culled while still
partly on screen:

```cpp
material.setBoundsPadding(2.0f);   // metres, on every side
```

#### Anti-aliasing

Edges are smoothed with FXAA, on by default. The scene is drawn into an
off-screen target the size of the window's viewport and then copied to the
window through the `post_process` shader (`assets/shaders/post_process.*`, loaded by the render
plugin). It works the same on OpenGL and Vulkan, costs one extra full-screen
pass, and also softens edges multisampling would miss, such as alpha-tested
leaves. The debug UI is drawn afterwards, so it stays sharp.

```cpp
auto &settings = world.getResource<resources::RenderSettings>();
settings.antiAliasing = resources::AntiAliasing::None;  // draw straight to the window
```

It can be changed while the game runs, from code or from the debug UI's
"Settings" tab, which also switches the depth pre-pass and LOD dithering.

`settings.renderScale` (default 1, range 0.25 to 2) draws the scene at that
fraction of the window's size and scales it to fit: below 1 trades sharpness
for speed, above 1 supersamples (2 averages 2x2 pixels per window pixel, at
four times the fragment cost). It uses the same off-screen target and shader as
FXAA, with or without FXAA on top, and has a slider in the Settings tab.

#### The sky

Add a `SkyboxComponent` to any entity. The first one found is drawn behind
everything as a full-screen triangle; it needs no mesh.

```cpp
world.addComponents(sky, renderer::SkyboxComponent{
    .color = Vec3(0.25f, 0.5f, 0.9f),    // overhead
    .horizonFromFog = true});            // horizon follows the fog colour
```

| Field | Default | Meaning |
|---|---|---|
| `shaderName` | `"skybox"` | shader to draw the sky with |
| `color` | (0.5, 0.7, 1.0) | colour straight overhead (`skyColor` in the shader) |
| `horizonColor` | (0.7, 0.8, 0.95) | horizon colour, used when `horizonFromFog` is false |
| `horizonFromFog` | `true` | use the `Fog` colour at the horizon so fogged ground fades into the sky |

A custom sky shader is loaded like any other (`loadShader`, then set
`shaderName`). Its contract:

- The vertex shader gets no vertex data and derives its three positions from
  `gl_VertexID`. Copy `assets/shaders/skybox_procedural.vert` and change only
  what you need; keep `gl_Position = vec4(ndc, 1.0, 1.0)` so the sky sits at the
  far plane.
- Loose uniforms set every frame: `mat4 invSkyViewProj` (inverse of projection
  times rotation-only view; unprojecting a pixel gives its view direction),
  `vec3 skyColor`, `vec3 horizonColor`.
- Per-frame values (`lightDir`, `lightColor`, fog, camera, time) come from the
  `FrameData` block, which the shader declares.
- Drawn with culling off, depth test `<=`, and depth writes off.

### Attaching assets to an entity

```cpp
ecs::EntityID tree = world.createEntity();
world.addComponents(tree,
    componeng::components::TransformComponent{
        .position = {10.0f, 0.0f, -5.0f},
        .scale    = {2.0f, 2.0f, 2.0f}
    },
    componeng::renderer::MeshComponent{
        .meshName = "tree",   // resolved to meshID at runtime
        .visible  = true
    },
    componeng::renderer::MaterialComponent{
        .materialName = "grass_mat"
    },
    componeng::renderer::ColorComponent{
        .color = {1.0f, 1.0f, 1.0f, 1.0f}
    }
);
```

Note: `MeshComponent.meshName` and `MaterialComponent.materialName` are serialized
names; the engine resolves them to `meshID`/`materialID` handles. You can also set
the handles directly if you load assets yourself.

---

## 8. Physics

The `PhysicsSystem` runs in the Simulation group at a fixed 60 Hz timestep with an
accumulator. It does gravity integration, broad-phase (currently O(n²)), narrow-phase
(AABB/AABB, sphere/sphere, AABB/sphere), and impulse-based collision response.

Make an entity physical:

```cpp
world.addComponents(entity,
    componeng::components::TransformComponent{.position = {0, 10, 0}},
    componeng::physics::RigidBodyComponent{
        .type = componeng::physics::RigidBodyComponent::Dynamic,
        .mass = 1.0f,
        .restitution = 0.5f
    },
    componeng::physics::ColliderComponent{
        .type = componeng::physics::ColliderType::Box,
        .transform = componeng::components::TransformComponent{}
    }
);
```

RigidBody types: `Static`, `Dynamic`, `Kinematic`. Collider types: `Box`, `Sphere`.

Collision events are emitted via the event bus (`collision_event.hpp`); systems can
read them with `world.getEvents<CollisionEvent>()`.

---

## 9. Audio

Audio uses miniaudio. The `AudioManager` is a resource: register a sound file once
under a name, then play it through an `AudioComponent` or directly.

```cpp
auto& audio = world.getResource<componeng::audio::AudioManager>();
audio.registerSound("assets/audio/boop.wav", "boop");
// Long files (music, ambience) can stream from disk instead of living in memory:
audio.registerSound("assets/audio/wind.mp3", "wind", /*stream=*/true);

ecs::EntityID snd = world.createEntity();
world.addComponents(snd,
    componeng::components::TransformComponent{.position = {0,0,0}},
    componeng::audio::AudioComponent{
        .audioName = "boop",
        .loop = true,
        .is3D = true,
        .minDistance = 1.0f,
        .maxDistance = 100.0f
    }
);
```

`AudioComponent` is driven by its `play` flag. `playOnAwake` (default true) sets it when
the sound is created; after that, set `play = true` to start or resume and `false` to
pause. `isPlaying` reports what is audible. A sound that is not looping clears `play`
when it ends. Volume, pitch and loop are re-read every frame, so they can be changed
live. With `is3D = false` the sound ignores the listener (music, ambience); with
`is3D = true` it is positioned from the entity's transform.

For one-shots, skip the component:

```cpp
audio.playOneShot("boop", /*volume=*/0.8f);                   // not positioned
audio.playOneShotAt("boop", x, y, z, 0.8f, 1.0f, 1.0f, 50.0f); // at a point
```

Lower level: `createSound(name, spatialized, autoRelease)` returns a handle and
`getSound(id)` the `Sound` to configure; instances made with `autoRelease` (the default)
are reclaimed once they have played to the end, others live until `releaseSound(id)`.
`setMasterVolume()` scales everything.

---

## 10. Events

The double-buffered event bus decouples systems.

```cpp
struct DamageEvent {
    ecs::EntityID target;
    ecs::EntityID attacker;
    float amount;
};

// Emit
state.world->emitEvent<mygame::DamageEvent>({target, attacker, 10.0f});

// Receive (next frame's read buffer)
const auto& events = state.world->getEvents<mygame::DamageEvent>();
for (const auto& e : events) {
    auto& hp = state.world->getComponent<mygame::HealthComponent>(e.target);
    hp.current -= e.amount;
}
```

---

## 11. Complete example: a small scene

This puts it all together — a camera, a ground plane, some crates, and a player
controller.

```cpp
// main.cpp
#include "componeng/core/default_plugins.hpp"
#include "componeng/core/engine.hpp"
#include "componeng/components/transform_component.hpp"
#include "componeng/renderer/component/mesh_component.hpp"
#include "componeng/renderer/component/material_component.hpp"
#include "componeng/renderer/component/color_component.hpp"
#include "componeng/renderer/asset_manager.hpp"
#include "componeng/physics/rigidbody_component.hpp"
#include "componeng/physics/collider_component.hpp"
#include "componeng/camera/main_camera.hpp"
#include "player_controller.hpp"

using namespace componeng;

class Game : public core::IPlugin {
public:
    void build(core::Engine& engine) override {
        engine.world().registerSystem<PlayerController>(ecs::SystemGroup::Simulation);
    }

    void ready(ecs::World& world) override {
        using namespace componeng::renderer;
        using namespace componeng::physics;

        // Load assets
        AssetManager::loadShader("assets/shaders/diffuse.vert",
                                 "assets/shaders/diffuse.frag",
                                 nullptr, "default");
        AssetManager::loadTexture("assets/textures/white.png", false, "white");
        AssetManager::registerMaterial<DiffuseMaterial>("default_diffuse", "default", "white");
        AssetManager::loadMesh("assets/models/cube.obj", "cube");

        // Main camera
        ecs::EntityID cam = world.createEntity();
        world.addComponents(cam,
            core::TransformComponent{.position = {0, 5, 10}, .rotation = {0,0,0}},
            camera::CameraComponent{.fov = 45.0f, .aspectRatio = 16.0f/9.0f,
                                    .nearPlane = 0.1f, .farPlane = 10000.0f});
        world.setResource(camera::MainCamera{cam});

        // Ground
        ecs::EntityID ground = world.createEntity();
        world.addComponents(ground,
            core::TransformComponent{.position = {0,-1,0}, .scale = {20,1,20}},
            MeshComponent{.meshName = "cube", .visible = true},
            MaterialComponent{.materialName = "default_diffuse"},
            ColorComponent{.color = {0.4f, 0.6f, 0.4f, 1.0f}},
            RigidBodyComponent{.type = RigidBodyComponent::Static},
            ColliderComponent{.type = ColliderType::Box});

        // 50 crates
        for (int i = 0; i < 50; ++i) {
            ecs::EntityID crate = world.createEntity();
            float x = (i % 10) * 2.5f - 12.5f;
            float z = (i / 10) * 2.5f - 12.5f;
            world.addComponents(crate,
                core::TransformComponent{.position = {x, 2, z}, .scale = {1,1,1}},
                MeshComponent{.meshName = "cube", .visible = true},
                MaterialComponent{.materialName = "default_diffuse"},
                ColorComponent{.color = {0.8f, 0.5f, 0.3f, 1.0f}},
                RigidBodyComponent{.type = RigidBodyComponent::Dynamic,
                                   .mass = 1.0f, .restitution = 0.4f},
                ColliderComponent{.type = ColliderType::Box});
        }

    }
};

int main() {
    core::Engine engine;
    engine.add(core::DefaultPlugins{}).add(Game{}).run();
    return 0;
}
```

The `PlayerController` system (from the example) reads `InputState`/`ActionState`
and `MainCamera`, then moves the camera transform with WASD + mouse.

---

## 12. Scene serialization

Scenes can be saved to / loaded from JSON:

```cpp
#include "componeng/ecs/scene_serializer.hpp"

SceneSerializer::save(world, "assets/scenes/my_scene.json");
SceneSerializer::load(world, "assets/scenes/my_scene.json");
```

Components are serialized via the `SERIALIZABLE_COMPONENT` macro and a field list
(see `transform_component.hpp` for the pattern). Runtime-only handles
(`meshID`, `materialID`, etc.) are not serialized — only the name fields are.

---

## 13. High-quality forest scene: what's needed

The engine is a solid foundation, but a "super high quality forest" (dense grass,
good lighting, shadows, large view distance) exceeds what it currently renders.
Below is a breakdown of what a forest scene needs and where ComponEng stands today.

### A. Geometry & instancing (what the engine already supports well)

A forest is fundamentally an instancing problem: thousands of trees + millions of
grass blades, mostly identical meshes at different positions/scales.

| Feature | Engine status | Notes |
|---------|---------------|-------|
| GPU instancing (`glDrawElementsInstanced`) | ✅ Supported | `RenderSystem` batches by (mesh, texture, shader) and draws instanced |
| Dynamic batching | ✅ Supported | `BatchingSystem` groups visible entities per draw key |
| Frustum culling | ✅ Supported | `CullingSystem` rejects entities outside the view frustum |
| Per-instance model + color | ✅ Supported | `DiffuseMaterial` writes `instanceModel` + `instanceColor` |
| LOD (levels of detail) | ❌ Not present | Need multiple meshes per tree and a system that picks by distance |
| Billboard / impostor grass | ❌ Not present | Grass should fade to camera-facing quads at distance |

**What to add for a forest:**
- A `GrassComponent` / `LODComponent` storing LOD levels, billboard flag, wind parameters.
- A system that, per frame, chooses mesh/LOD based on camera distance and populates
  separate instance buffers per LOD level.
- Grass: thousands of quads, camera-facing (billboarded) in the vertex shader, with
  a wind uniform bending them. Use instancing + per-instance position/scale/color.
- Trees: use real meshes up close, switch to impostors/billboards beyond ~100 m.

### B. Lighting (needs the most work)

Current lighting: the diffuse shader has one directional light (`lightDir`, `lightColor`)
plus a hardcoded 0.1 ambient term. That's "flat daytime" lighting — fine for a demo,
not for a forest.

| Feature | Engine status | What's needed |
|---------|---------------|---------------|
| Directional light (sun/moon) | ⚠️ Partial | `DirectionalLightComponent` exists but the shader only takes one hardcoded lightDir/color |
| Point lights | ❌ Not used by renderer | `PointLightComponent` exists but nothing consumes it |
| Shadows (shadow maps) | ❌ Not present | Need depth-pass rendering + shadow-map samplers in shaders |
| Ambient light / environment | ❌ Not present | Add `ambientColor` uniform, or a skybox + IBL |
| Fog | ❌ Not present | Add fog uniform + distance-based fade in fragment shader |
| Normal maps / PBR | ❌ Not present | `DiffuseMaterial` is Lambert-only |

**Recommended lighting upgrade for a forest:**
1. Extend `DiffuseMaterial` (or add a `PbrMaterial`) to accept uniforms:
   - `lightDir`, `lightColor` (directional sun)
   - `ambientColor` (sky light)
   - `fogColor`, `fogDensity` / `fogStart`, `fogEnd`
   - optional `lightViewProj` + shadow sampler
2. Add a `LightSystem` that aggregates `DirectionalLightComponent` / `PointLightComponent`
   into a `Lighting` resource consumed by the shader.
3. Render a shadow-map pass: render the scene from the light's perspective into a depth
   texture, then sample it in the diffuse shader for PCF-filtered shadows.
4. Add a skybox (skybox shaders already exist in `assets/shaders/skybox.*`) as the
   background and ambient source.
5. Add fog so distant trees fade into the skybox (critical for large forests and
   performance via early culling).

### C. Materials & texturing

- `MaterialComponent` supports one texture + one shader per material. A forest needs:
  - Albedo (tree bark, grass, ground)
  - Normal maps (bark detail, grass clumps)
  - Possibly roughness/metallic if you go PBR
- Texture arrays or atlases would let you batch many different tree/grass types in
  one draw call instead of switching textures.

### D. Terrain

The engine has no terrain system. For a forest you need:
- A heightmap-based terrain mesh (or imported `.obj` terrain)
- A `TerrainComponent` with height lookup (for placing trees/grass on the surface
  and for player collision)
- Ground texture splatting (grass/dirt/rock layers)

### E. Animation (wind)

- Grass and leaves should sway. Declare the `FrameData` block in a custom
  vertex shader (see "Per-frame values in custom shaders") and offset vertices
  by `sin(time + position)`, using the block's `time`.

### F. Performance considerations for a forest

| Technique | Why it matters |
|-----------|----------------|
| Frustum culling (already present) | Don't render trees behind the camera |
| LOD + impostors | Far trees = cheap billboards |
| Occlusion culling | Trees behind other trees/terrain |
| Instancing per (mesh, material) | Minimize draw calls |
| Thread pool for instance-data builds | `eachParallel` exists; use it for grass |
| Far-plane / fog | Limits visible geometry |

---

## 14. Quick reference: built-in types

```cpp
// Math
componeng::core::Vec3, Vec4, Mat4, Vec2
componeng::core::radians(deg), normalize(), cross(), dot(), lookAt(), perspective()

// Components
componeng::components::TransformComponent
componeng::renderer::MeshComponent, MaterialComponent, ColorComponent
componeng::renderer::DirectionalLightComponent, PointLightComponent
componeng::camera::CameraComponent
componeng::physics::RigidBodyComponent, ColliderComponent
componeng::audio::AudioComponent

// Resources
componeng::resources::InputState, ActionState, MainCamera
componeng::renderer::AssetManager (static)
componeng::audio::AudioManager

// Systems
componeng::ecs::ISystem, SystemState, SystemGroup::{PreUpdate,Simulation,Presentation,FrameEnd}
```

### Where to find things

```
include/componeng/
├── core/              engine.hpp, plugin.hpp, default_plugins.hpp, transform_component.hpp, types.hpp, window.hpp
├── ecs/               world.hpp, system.hpp, query.hpp, scene_serializer.hpp
├── components/        (builtin components)
├── renderer/          asset_manager.hpp, render_system.hpp,
│   ├── component/     material_component.hpp, mesh_component.hpp, light_component.hpp
│   ├── asset/         diffuse.hpp, shader.hpp, mesh.hpp, texture.hpp, material.hpp
│   ├── batching/      batching_system.hpp, batch_map.hpp
│   ├── culling/       culling_system.hpp, frustum.hpp
│   └── backend/       opengl (gl_shader, gl_texture, gl_mesh, gl_render_device)
├── physics/           physics_system.hpp, collision_detection.hpp
├── audio/             audio_system.hpp, audio_manager.hpp
├── camera/            camera_system.hpp, main_camera.hpp, camera_component.hpp
└── resources/         input_state.hpp, action_state.hpp, main_camera.hpp
```

Happy building! If you're planning the forest scene specifically, start by adding
fog + a second directional-light uniform to the diffuse shader, then build an
instanced grass system with billboard falloff — those two changes give 80% of the
"forest feel" with the least engine work.
