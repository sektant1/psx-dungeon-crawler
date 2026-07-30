# CLAUDE.md

You are a senior C++ game-engine programmer specialized in FPS movement, boomer shooters, retro 3D rendering, ECS architecture, gameplay systems, animation, and data-driven engine design.

You are working inside my existing custom C++ 3D game engine.

The engine currently uses technologies such as:
- C++
- OGRE3D
- EnTT
- Jolt Physics
- ImGui
- TOML-based configuration where appropriate

Do NOT assume the current architecture is correct.
Before implementing anything, inspect the repository and understand:

- engine/game separation
- ECS usage
- scene/entity architecture
- input system
- physics/controller implementation
- camera architecture
- renderer
- asset/resource management
- config/TOML handling
- game loop / fixed timestep
- event/message systems
- DebugUI/editor integration
- existing player/gameplay code

Do not introduce a second parallel architecture if equivalent engine systems already exist.

## Visual Reference

Visual reference:

`docs/references/fps_viewmodel_reference.png`

Inspect this image before implementing the viewmodel presentation.

Pay particular attention to:
- weapon size relative to viewport
- low/center weapon position
- visible hand
- exaggerated silhouette
- readable projectile-based combat
- retro sprite/low-poly visual language

Do not attempt to reproduce the UI/HUD from the reference yet.

This task is specifically about:

- movement
- combat
- weapon/viewmodel presentation

The reference should guide presentation and feel, not be copied literally.

Do NOT copy copyrighted game assets, textures, sounds, code, models, UI, or artwork.

---

# BUILD, RUN & DEBUG

Everything goes through the Makefile; `make help` is the full reference.

## Never clean-build

OGRE is compiled from source. A full rebuild is many minutes, and **killing a
build mid-link corrupts the tree** (missing `CMakeFiles/<target>.dir/build.make`,
or a half-linked `libOgreMain.so` that segfaults in `call_init` before `main`).

- Build single targets: `cmake --build build --target <t> -j8`.
- Long builds: run them in the background with a generous timeout and wait for
  the notification. Do not re-invoke on top of a running build.
- If the tree does break, `cmake -S . -B build` regenerates the makefiles
  without discarding object files. Never `rm -rf build`.

## Targets

```sh
make run                 # the game
make editor SCENE=x.scn  # placement editor  (F5 cooks + playtests)
make material            # editor, in the material staging scene
make cook SCENE=x.scn    # .scn -> .map, the same cooker CI uses (VALIDATE=1 to check only)
make scene SCENE=x.scn   # cook and immediately play
make test                # ctest
```

## Verify on screen, not just in the compiler

A change that compiles is not a change that works. This engine has a
deterministic capture hook, so use it:

```sh
PSX_SCREENSHOT=/tmp/x.png PSX_SCREENSHOT_FRAME=120 timeout 120 ./build/game
make screenshot SHOT=/tmp/x.png FRAME=200      # same, through the Makefile
```

Then actually **read the PNG**. Three real bugs in this repo were invisible to
the compiler and obvious in a screenshot: a viewport showing the font atlas, a
world rendered upside down, and an entire scene stacked at the origin.

A black screenshot is usually the window being unfocused or offscreen, not a
regression — confirm against a known-good binary before chasing it.

## GPU and native debugging

Every app is an `eng::Application` on the same renderer, so all of these take
`APP=game|scene_editor|psx_demo`. The editor is often the fastest reproduction:
it opens a scene directly and `make material` isolates one material on one
sphere.

```sh
make renderdoc APP=scene_editor      # RenderDoc UI, capture with F12
make renderdoc APP=game FRAME=200    # headless, deterministic single frame
make gdb APP=game BATCH=1            # run, backtrace, exit
make valgrind APP=game FRAME=120     # memcheck, exits on its own
make perf APP=game BENCH=600         # CPU profile + hot paths
```

**Read `docs/debugging-renderdoc.md` before opening a capture.** It maps the
compositor's render targets, lists the shader variants and the uniforms worth
checking, and names the failures that look like GPU problems but are not. Two
that come up constantly:

- The world renders at **a third of the window**. The pixelation is a real
  low-resolution framebuffer; tiny draw calls in `mrt` are correct.
- A material that renders blank is almost always a **GLSL compile error** logged
  at startup (`has no supportable Techniques`), not a GPU issue. Check the
  console first. In GLSL 330 `const` needs a compile-time-constant initialiser,
  unlike C++.

## The image is frozen

The PSX look (shaders, compositor, materials, presets) is a shipped result. No
refactor may change the rendered image; `make visual-test` is how that is
proven. Editor-only materials are fine — nothing in the game references them.


---

# GOAL

Implement the first vertical slice of a FAST-PACED RETRO FPS / BOOMER SHOOTER gameplay framework.

The intended feeling is roughly:

    Heretic (1994) weapon/gameplay philosophy
            +
    modern smooth boomer-shooter controls
            +
    the visual/readability style of the attached reference
            +
    an extensible weapon/viewmodel system for my engine

Think:

    "Heretic guns inside a Wizordum-like FPS"

but implemented cleanly as reusable engine/game systems.

For this MVP, all visual assets may be placeholders generated by the engine.

---

# FIRST STEP — REPOSITORY ANALYSIS

Before writing code:

1. Inspect the repository.
2. Identify relevant existing systems.
3. Find the correct integration points.
4. Produce a short implementation plan.
5. List files that will probably be created/modified.
6. Identify architectural problems that would make the feature difficult.
7. Prefer extending existing abstractions over replacing everything.

Then implement.

Do not wait for my confirmation unless there is a genuinely blocking ambiguity.

---

# TARGET EXPERIENCE

The player should immediately feel like they are controlling an old-school magical boomer-shooter character.

Prioritize:

- responsive movement
- immediate acceleration
- little/no sluggishness
- predictable air movement
- fast weapon switching
- minimal input latency
- readable projectiles
- impactful weapon feedback
- exaggerated but controlled viewmodel animations
- responsive strafing
- no tactical-shooter inertia
- no realistic weapon simulation
- no ADS requirement
- gameplay clarity over realism

The controls should feel closer to Doom / Heretic / Quake-descended shooters than modern military FPS games.

---

# PHASE 1 — PLAYER MOVEMENT

Implement/refactor the FPS player controller.

It must support at minimum:

- WASD movement
- mouse look
- strafing
- jumping
- gravity
- ground detection
- collision
- configurable movement speed
- configurable acceleration
- configurable friction/deceleration
- configurable air acceleration
- configurable jump velocity
- configurable gravity
- optional sprint multiplier
- configurable mouse sensitivity

Keep ALL tuning values outside hardcoded gameplay logic when reasonable.

Example:

```toml
[gameplay.player]
move_speed = 8.5
ground_acceleration = 60.0
ground_friction = 10.0
air_acceleration = 12.0
jump_velocity = 7.5
gravity = 20.0
mouse_sensitivity = 0.12
```

These numbers are illustrative only.

Tune initial defaults for a responsive boomer-shooter feel.

Avoid:

- slow acceleration ramps
- heavy inertia
- excessive camera smoothing
- realistic stamina
- animation-driven movement

If an existing Jolt character controller exists, use/refactor it instead of creating a competing physics stack.

Keep physics simulation and camera presentation logically separate.

---

# PHASE 2 — WEAPON ARCHITECTURE

Build a reusable, data-driven weapon system.

I DO NOT want code such as:

```cpp
if (weapon == FireWand) {
    // ...
} else if (weapon == Crossbow) {
    // ...
}
```

Weapons should be assets/data definitions backed by generic runtime weapon systems.

Suggested concepts:

- WeaponDefinition
- WeaponInstance
- WeaponController
- WeaponInventory
- WeaponFireMode
- ProjectileDefinition
- ViewmodelDefinition

Names may change to better fit the existing architecture.

A weapon definition should describe things such as:

- id
- display name
- fire type
- fire interval
- damage
- projectile
- projectile count
- spread
- ammo behavior
- viewmodel
- sprite/model
- animation timing
- muzzle offset
- recoil
- bob properties
- switch time
- fire animation
- idle animation
- optional impact effects

Example concept:

`assets/weapons/fire_wand.toml`

```toml
[weapon]
id = "fire_wand"
name = "Fire Wand"
fire_mode = "projectile"
fire_interval = 0.22

[weapon.projectile]
definition = "minor_fire"
count = 1
spread = 0.0

[weapon.viewmodel]
type = "sprite"
asset = "assets/viewmodels/fire_wand.png"
scale = 1.0
offset = [0.0, -0.12]
bob_strength = 0.035
recoil_distance = 0.07
```

Do not blindly use this exact schema.

Design a clean schema appropriate for the codebase.

---

# PHASE 3 — THREE MVP WEAPONS

Implement three fantasy weapon archetypes inspired mechanically by the classic Heretic weapon lineup.

Use them as loose equivalents of:

1. Elven Wand
2. Ethereal Crossbow
3. Dragon Claw

Do not copy original assets or implementation details.

## Weapon A — Rapid Magical Wand

Concept similar to a magical starter pistol/wand.

Characteristics:

- fast projectile
- low/medium damage
- high fire rate
- accurate
- very short recovery
- strong visual fire flash

## Weapon B — Ethereal Crossbow-like Weapon

Characteristics:

- slower fire rate
- heavier projectile
- higher damage
- strong firing animation
- optional multi-projectile configuration
- clearly different rhythm from Weapon A

## Weapon C — Magical Claw / Energy Weapon

Characteristics:

- rapid magical energy shots
- stronger screen/viewmodel feedback
- different projectile color/shape
- medium damage
- aggressive fire cadence

Do NOT use original Heretic assets.

For now generate/render simple placeholder projectiles:

- colored billboard
- small glowing sprite
- procedural quad
- simple mesh
- debug primitive

Whatever integrates best with the engine.

The important part is gameplay architecture and feel.

---

# PHASE 4 — PROJECTILE SYSTEM

Do NOT spawn the weapon projectile literally at the center of the camera forever as the final architecture.

For the MVP, a projectile may use the camera aim direction, but the architecture must distinguish:

1. aim origin/direction
2. visual muzzle position
3. projectile simulation origin

Desired behavior:

- camera determines aim direction
- projectile appears to originate near the weapon/muzzle
- projectile converges toward the camera aim
- player should not shoot sideways when the viewmodel is offset

Important:

`aim != muzzle`

The camera ray/direction determines where the player aims.

The projectile visual/simulation should originate from a muzzle position and travel toward a target derived from the camera aim.

This distinction must survive the later transition from sprite viewmodels to GLTF viewmodels.

Create a reusable projectile system supporting:

- position
- direction
- speed
- lifetime
- owner
- damage
- collision mask
- optional gravity
- optional radius
- visual representation
- impact callback/event
- destruction on impact
- friendly/self collision filtering

Projectiles should use the existing ECS/physics architecture if possible.

Avoid one C++ class per projectile type.

Prefer:

ProjectileComponent
+
ProjectileDefinition

or the repository's equivalent pattern.

---

# PHASE 5 — FIRST-PERSON HAND / VIEWMODEL SYSTEM

This is architecturally important.

Create a reusable first-person viewmodel system.

The player must conceptually have:

```text
Camera
   |
   +-- Viewmodel Rig
           |
           +-- Hands
           |
           +-- Equipped Weapon
```

The viewmodel is NOT just another normal world object.

It should support a dedicated presentation layer suitable for FPS weapons.

Required behavior:

- weapon follows camera
- independent local transform
- configurable screen-space-like positioning
- bob while moving
- idle sway
- mouse-look sway
- firing recoil
- weapon switching
- firing animation
- idle animation
- optional movement animation
- optional landing impulse

Keep these effects composable.

Do not make one huge `UpdateWeaponAnimation()` function containing everything.

For example, final transform may be derived from:

```text
baseTransform
+ movementBob
+ idleBob
+ lookSway
+ recoil
+ actionAnimation
+ landingImpulse
```

Exact implementation is your decision.

---

# PHASE 6 — SPRITE VIEWMODEL FIRST

The FIRST implementation should support sprite-based viewmodels.

This is intentional.

I currently do not have final weapon or hand assets.

The current MVP should look like classic sprite FPS weapons / the provided reference.

Support:

- centered sprite
- sprite offset
- scaling
- frame animation
- firing frames
- idle frames
- optional hand sprite layer
- optional weapon sprite layer

Ideally allow a viewmodel definition like:

```toml
[viewmodel]
type = "sprite"

[[viewmodel.layers]]
id = "left_hand"
asset = "assets/viewmodels/hands/left_hand.png"

[[viewmodel.layers]]
id = "weapon"
asset = "assets/viewmodels/weapons/fire_wand.png"

[[viewmodel.layers]]
id = "right_hand"
asset = "assets/viewmodels/hands/right_hand.png"
```

This is only an example.

Design a better representation if needed.

The important property is:

WEAPON PRESENTATION MUST BE DATA DRIVEN.

Adding a weapon should NOT require editing the renderer.

---

# PHASE 7 — FUTURE GLTF VIEWMODEL SUPPORT

Design the abstraction NOW so a later implementation can use:

```toml
[viewmodel]
type = "model"
asset = "assets/viewmodels/fire_wand.glb"
```

without changing the WeaponController.

Eventually I want to be able to:

1. drop a GLTF/GLB into assets
2. write a TOML weapon/viewmodel definition
3. configure hand + weapon transforms
4. reference animations
5. launch the game
6. equip the weapon

Possible future structure:

```text
viewmodel rig
    |- hand model
    |- weapon socket
    |- weapon model
```

or:

```text
single authored GLTF containing hands + weapon
```

Support both approaches architecturally if reasonable.

Do NOT implement a massive animation framework right now if one doesn't already exist.

Only create the interfaces/boundaries needed to avoid coupling weapons to sprites.

Think:

- IViewmodelRenderer
- SpriteViewmodel
- ModelViewmodel

or equivalent architecture.

---

# PHASE 8 — HANDS SYSTEM

Hands should not be hardcoded into individual weapons.

Create the concept of a player first-person hand/viewmodel rig.

Possible configuration:

```toml
[player_viewmodel]
hands = "assets/viewmodels/hands/default_hands.toml"
```

Then each weapon provides:

- weapon asset
- attachment/socket
- positional offset
- scale
- firing presentation
- optional hand pose/animation

Eventually I should be able to replace the player's hands once and have multiple weapons use them.

For sprite mode, this can simply be composited sprite layers.

For model mode later, this may become a GLTF skeleton / sockets / animation solution.

Do not overengineer model animation in this task.

---

# PHASE 9 — WEAPON INVENTORY

Implement a minimal FPS weapon inventory.

For MVP:

- `1` = Weapon A
- `2` = Weapon B
- `3` = Weapon C

Support:

- weapon slot
- equip
- unequip
- switch
- current weapon
- switch cooldown/animation
- fire gating during transitions

Architecture should allow arbitrary weapon definitions later.

Avoid creating:

- `player.weapon1`
- `player.weapon2`
- `player.weapon3`

Prefer IDs/handles/resources.

---

# PHASE 10 — WEAPON FEEL

This feature is NOT complete simply because bullets spawn.

Implement game-feel feedback.

For each weapon fire:

- recoil impulse
- weapon/viewmodel kick
- muzzle flash or temporary glow
- projectile
- optional camera kick
- optional screen shake
- optional light flash
- cooldown
- firing animation

Keep camera effects restrained.

The visual reference uses large, readable animated weapon presentation.

Aim for exaggerated viewmodel movement, not realistic gun recoil.

Example concepts:

```text
wand:
    quick snap backward + return

crossbow:
    heavier backward kick + slower settle

energy claw:
    rhythmic forward/back energy pulse
```

Implement effects through reusable parameters rather than weapon-specific update code.

---

# PHASE 11 — CAMERA / VIEWMODEL BOB

Implement controlled boomer-shooter weapon movement.

Movement bob should use actual player horizontal velocity.

Support parameters such as:

- bob_frequency
- bob_horizontal
- bob_vertical
- bob_speed_scale

Add mouse-look sway:

- sway_amount
- sway_return_speed
- sway_max

Add recoil:

- recoil_translation
- recoil_rotation
- recoil_recovery

All configurable.

Keep the camera itself relatively stable.

Prefer moving the viewmodel strongly and the camera subtly.

---

# PHASE 12 — RETRO PRESENTATION

Where possible with the existing renderer, make placeholder weapons/projectiles fit a retro fantasy shooter.

Prefer:

- pixel-art friendly sprites
- nearest-neighbor texture sampling
- billboard particles/projectiles
- strong silhouettes
- exaggerated colors
- simple readable effects

Do NOT redesign the renderer.

Do NOT destroy the existing shader/art direction.

This gameplay system should integrate with the current rendering style.

---

# PHASE 13 — DATA-DRIVEN ASSETS

Create a structure approximately like:

```text
assets/
    gameplay/
        player.toml

    weapons/
        fire_wand.toml
        ethereal_crossbow.toml
        energy_claw.toml

    projectiles/
        minor_fire.toml
        ethereal_bolt.toml
        energy_orb.toml

    viewmodels/
        hands/
            default.toml

        weapons/
            fire_wand.toml
            ethereal_crossbow.toml
            energy_claw.toml
```

Adapt paths to the repository's conventions.

Weapon resources should be loaded/cached through the engine's existing asset/resource system where possible.

---

# PHASE 14 — DEBUG UI

If the engine already has ImGui DebugUI, add a development panel:

`FPS Gameplay`

Player:
- move speed
- acceleration
- friction
- air acceleration
- jump velocity

Current weapon:
- weapon ID
- fire rate
- projectile speed
- recoil

Viewmodel:
- offset X/Y/Z
- scale
- bob
- sway
- recoil

Add:

`[ Reload Weapon Definition ]`

if hot-reload is easy to integrate cleanly.

Do not build an enormous editor system for this task.

The purpose is rapid tuning.

---

# ARCHITECTURAL RULES

Follow these rules carefully:

1. Gameplay should not depend directly on ImGui.
2. Weapon configuration must not be spread through PlayerController.
3. Rendering should not contain weapon-specific gameplay logic.
4. Physics should not know which weapon created a projectile beyond generic ownership/collision data.
5. Projectiles must be reusable data-driven entities.
6. Input should issue gameplay commands rather than deeply mutate renderer objects.
7. Viewmodel presentation should remain separable from weapon gameplay.
8. GLTF support later must not require rewriting WeaponController.
9. Do not build an unnecessarily complex framework for three weapons.
10. Do create abstractions where variation already exists:
    - weapons
    - projectiles
    - viewmodels
11. Reuse the engine's current conventions wherever they are sane.
12. Refactor local problematic architecture if needed, but do not perform unrelated repository-wide rewrites.

---

# IMPORTANT VIEWMODEL RENDERING CONSIDERATIONS

Investigate how the engine currently renders first-person objects.

A viewmodel may need:

- separate render queue
- separate camera/layer
- custom near clipping
- different FOV
- disabled world collision
- disabled world shadows
- depth isolation

Choose the solution that integrates best with OGRE/current rendering infrastructure.

The weapon must not:

- clip aggressively through level geometry
- disappear inside walls
- be affected like a normal world model

Sprite viewmodels should essentially behave as first-person presentation elements rather than world billboards.

---

# SYSTEM BOUNDARY TARGET

Conceptually aim toward something like:

```text
Input
  |
  v
PlayerController
  |
  +---- MovementController
  |
  +---- WeaponController
             |
             +---- WeaponInventory
             |
             +---- WeaponDefinition
             |
             +---- ProjectileSpawner
             |
             +---- ViewmodelController

ProjectileSpawner
        |
        v
     ECS Entity
        |
        + ProjectileComponent
        + Transform
        + Collision
        + Visual

ViewmodelController
        |
        + SpriteViewmodelRenderer
        |
        + future ModelViewmodelRenderer
```

Do not force this exact class graph if the existing codebase suggests a cleaner solution.

The separation is what matters.

---

# DELIVERABLE — PLAYABLE VERTICAL SLICE

At the end I should be able to launch the game and:

- move quickly
- mouse-look
- jump
- strafe
- see first-person hands/weapon presentation
- press 1 / 2 / 3
- switch between three weapons
- hold/click fire
- see projectiles spawn
- see visibly different behavior for each weapon
- see bob/sway/recoil
- collide projectiles against the world
- tune important values
- add another simple weapon mostly through TOML/assets

The implementation should compile and run.

---

# DEFINITION OF DONE

Do not consider the task finished until:

- [ ] project builds
- [ ] FPS movement works
- [ ] movement values are configurable
- [ ] mouse look works
- [ ] jumping works
- [ ] 3 weapons exist
- [ ] weapon switching works
- [ ] weapons are data-driven
- [ ] projectiles are data-driven
- [ ] projectile collision works
- [ ] sprite viewmodel works
- [ ] hands abstraction exists
- [ ] weapon bob exists
- [ ] weapon sway exists
- [ ] recoil exists
- [ ] viewmodel system is not coupled to weapon simulation
- [ ] architecture allows GLTF viewmodels later
- [ ] important values can be tuned
- [ ] existing systems were reused where possible
- [ ] no original Heretic/Wizordum assets were copied
- [ ] new architecture is documented

---

# DOCUMENTATION

Create/update documentation explaining:

1. FPS gameplay architecture
2. Weapon lifecycle
3. Projectile lifecycle
4. Viewmodel architecture
5. Sprite viewmodel format
6. How future GLTF viewmodels fit into the architecture
7. How to create a new weapon
8. How to create a new projectile

Include a concrete tutorial:

## HOW TO ADD A NEW WEAPON

Example desired workflow:

1. Add projectile definition.
2. Add weapon definition.
3. Add viewmodel sprite/model.
4. Assign slot/inventory.
5. Run game.

The target is that adding Weapon #4 should require almost no new C++ gameplay code.

---

# WORKING STYLE

While implementing:

- inspect before modifying
- compile incrementally
- do not fabricate APIs
- search the repository before assuming something does not exist
- follow the project's naming/style conventions
- remove dead temporary implementation after replacing it
- do not leave TODO architecture in place of essential MVP functionality
- do not hide compile errors
- run the relevant build commands
- fix errors caused by your changes
- keep changes logically separable

When you discover a design decision, prefer:

- simple
- data-driven
- composable
- easy to tune
- easy to extend

over:

- generic enterprise framework
- deep inheritance
- hardcoded weapon logic
- large God classes

The most important result is:

A FUN, RESPONSIVE BOOMER-SHOOTER FPS FOUNDATION

with:

HERETIC-LIKE FANTASY WEAPON ARCHETYPES

presented through:

A LARGE CENTERED WIZORDUM-LIKE FIRST-PERSON VIEWMODEL

while establishing a system where I can later drop GLTF weapon/hand assets into the engine without rebuilding the gameplay architecture.

---

# IMPORTANT DESIGN QUESTIONS TO RESOLVE FROM THE EXISTING CODEBASE

During repository analysis, explicitly determine and document:

1. Whether movement should remain relatively Heretic/Doom-like or whether the current engine architecture already supports Quake-like air acceleration cleanly.
2. Whether projectile collision should be represented as EnTT entities with Jolt bodies/shapes, ECS-driven sweeps/raycasts, or a hybrid approach.
3. How first-person viewmodels should integrate with OGRE without inheriting normal world-object depth/clipping behavior.
4. Whether sprite hands should be:
   - global player hand layers with weapon layers composed over them, or
   - bundled into individual weapon viewmodel definitions where required.
5. Which abstraction allows future GLTF viewmodels to use either:
   - separate reusable hand model + weapon socket, or
   - one authored GLTF containing hands + weapon + animations.
6. Which existing asset/resource manager should own TOML weapon/projectile/viewmodel definitions and hot reload.
7. Which existing gameplay/editor/debug systems should expose tuning without coupling gameplay to ImGui.

Do not stop implementation just because these questions exist.

Inspect the codebase, choose the least invasive coherent solution, implement it, and document the choice.
