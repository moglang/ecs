# Mog ECS

`github.com/moglang/ecs` is an external native-performance entity-component-
system package for Mog. Its storage engine is implemented in C++; it is not
built into or linked by the Mog language runtime. Import it with
`const ecs = @import("github.com/moglang/ecs")` and create an initialized world with
`ecs.CreateWorld()`.

Components are deterministic fixed-size byte records. Prefer `CreateSchema()`
and `world.componentFromSchema(...)` to manual byte packing. Schemas support
all fixed-width integer and float types, booleans, and entity IDs with
little-endian encoding. They intentionally reject managed runtime values.

Queries match one to three component types. Their dense order is unstable, and
any structural world change invalidates active queries. Replacing existing
component bytes through `Query.set` is nonstructural and safe during iteration.
Call `Query.close()` and `World.close()` when practical; GC finalizers are a
safe fallback and are idempotent with explicit closure.

```mog
const ecs = @import("github.com/moglang/ecs")

var world ecs.World = ecs.CreateWorld()
const position ecs.ComponentType = world.component("Position", 8u64, 4u64)

const entity u64 = world.create()
world.add(entity, position, [0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8])

world.close()
```

The repository contains the public source wrapper at its root and the private
`github:ecs-native` binding under `native/`. See `lib/README.md` for the
complete layout, lifetime, and C API rules.

Build the C++ package and its tests independently from Mog:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```
