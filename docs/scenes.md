# Scenes: what one is, and what it needs to work

A scene is not "a list of entities". It is a list of entities that fills a set
of **roles**, and which roles it fills decides what kind of scene it is.

For the object model see [ecs.md](ecs.md); for the editor see
[world-editor.md](world-editor.md); for the reasoning behind this file see
[design/2026-08-06-scene-contract-and-one-component-standard.md](design/2026-08-06-scene-contract-and-one-component-standard.md).

## The kinds

Everything is a scene — a dungeon level, a ten-second camera move, a main menu.
Which one it is is **derived, never authored**: it follows from the view
component on the camera entity, which is the same rule `MapRuntime` and
`MapPlay` already used before it had a name.

| The camera entity carries | Kind | What it is |
|---|---|---|
| nothing, and there is no player spawn | **Empty** | loads, cooks, plays, shows nothing |
| nothing, but there *is* a player spawn | **Level** | the game supplies the camera — what nearly every dungeon level is |
| `Camera` only | **Shot** | plays itself; the player controller stands down and the mouse stays free, which is what makes it recordable with `--record` |
| `FirstPersonController` | **First person** | a level seen from the player's eyes |
| `ThirdPersonCamera` | **Third person** | over the shoulder, with a lock-on |
| `ScreenCamera` | **2D screen** | not a world: a flat page authored in pixels |

Swapping between them is one operation — `setSceneView()`, or the Scene panel's
buttons. It **reuses the camera entity**, keeping its transform and its name: the
camera is *where it is*, and re-placing it is the part nobody wants to redo when
they are only changing the shape.

## The roles

`sceneContract()` returns this table for a document. The validator reads it, the
editor's Scene panel renders it, and the cooker refuses a scene that fails it —
one table, so those three cannot disagree.

| Role | Filled by | Required in | If empty |
|---|---|---|---|
| **View** | one of the four view components — **or a `PlayerSpawn`**, since the game drives the camera then | every scene | **Error** — nothing to look through |
| **Player spawn** | `PlayerSpawn` | world scenes | **Error** — the player has nowhere to start |
| **Audio listener** | `AudioListener` | any non-empty scene | Warning — the player camera hears instead |
| **Key light** | a directional `LightRef` | world scenes | Warning — only point lights will light it |
| **Environment** | the document's `palette` | any | Info — the game's default grading applies |
| **Exit** | `Exit` | world scenes | Info — the level does not end |

Two properties are worth knowing:

- **A role can be over-filled, and that is sometimes fine.** Two views is legal —
  a debug or death cam takes over by existing at a higher priority, and neither
  camera knows about the other. Two *audio listeners* is not: positional audio
  is undefined, and nothing else in the editor would have reported it.
- **Roles that do not apply are not reported.** A 2D screen needs no key light
  and no player spawn. Demanding them would train people to ignore the panel,
  which is the failure mode a checklist has.

Every unfilled required role carries a `QuickFix`, so "this scene has no camera"
comes with the button that adds one.

### What reaches the Problems list, and what does not

The Scene section shows all six roles. The **validator** reports only two kinds
of thing, and the distinction is deliberate:

- an **unfilled Error role** — no view, or a world with nowhere to start;
- an **over-filled role that cannot have two** — two audio listeners leaves
  positional audio undefined, and nothing else in the editor would say so.

The Warning-severity holes stay out of it. A scene with no authored audio
listener is normal (the player's camera hears), and a torch-lit dungeon has no
directional light by design. Reporting those would put two warnings on every
correct level in the repo, which is precisely how a panel earns being ignored.

## 2D scenes: menus, HUDs and UI

A scene carrying `ScreenCamera` is a flat page. The camera is fixed square-on to
the XY plane at exactly the distance where `pageHeight` virtual pixels fill the
view, so **entities are authored in pixels** and stay that size at every window
resolution: a 32×32 icon is a 32×32 quad.

Start one from **New scene ▸ 2D screen (menu / HUD)**, or make an existing scene
flat with the Scene panel's **2D screen** button.

| Field | Means |
|---|---|
| `pageWidth` / `pageHeight` | the design resolution, in virtual pixels |
| `fit = Contain` | the whole page is always visible, letterboxed. What a **menu** wants — nothing authored can be cropped |
| `fit = Height` | the page height always fills the screen; a wider window sees past the sides. What a **HUD** wants — the vertical layout never moves |
| `origin = TopLeft` | y down from the corner, the UI convention. `Centre` keeps things centred at any aspect |
| `layerSpacing` | world units between authored z layers, so "background, panel, icon, tooltip" is an integer |

The payoff is that a UI is a scene **like any other**: the same editor, the same
`.scn`, the same cooker, the same preview, the same undo, the same layers, the
same `Clip` animations. A menu that slides in is a clip on a panel, authored in
the Timeline exactly like a door.

### The two limits

- **The projection stays perspective.** Changing it is changing the renderer,
  and the renderer's image is frozen (see `CLAUDE.md`). Anything on the page
  plane is pixel-exact; layers pushed off it are very slightly scaled, which
  reads as depth and is why the spacing default is small.
- **This is layout, not widgets.** Text, bars and anything computed per frame
  still belong to `eng::ui::UiCanvas`, which is already pixel-exact and drawn
  over everything. What a screen scene adds is the ability to *author* the
  layout. A `Text` component and hit-testable buttons are the honest next step
  and are **not built** — today a menu is authored plates plus canvas text drawn
  over them.

## Reading a scene you did not author

`F1` ▸ **Scene** (or the editor's Scene panel) is the answer to "what does this
scene do":

- the kind, and a sentence saying what that means
- every role, what fills it, and the entity that does — click to select it
- what is missing, and the button that fixes it

## Checking it from the command line

```sh
make cook SCENE=x.scn VALIDATE=1     # contract + every per-entity rule
```

Contract failures appear as `scene.role_unfilled` and `scene.role_ambiguous`.
The codes are stable and greppable; the messages are free to be reworded.
