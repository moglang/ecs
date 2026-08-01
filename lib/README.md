# Mog ECS C++ library

This directory contains the interpreter-independent ECS storage library and its
C-compatible boundary. It belongs to the external `moglang/ecs` package
repository and is never linked into the Mog language runtime.

## Lifetime and mutation rules

- Entity IDs pack a 32-bit index in the low bits and a 32-bit generation in the
  high bits. Index reuse increments the generation, so stale entity IDs cannot
  address a replacement entity.
- World and query handles use independent generation-aware handle tables. They
  are never pointer casts. Destroying a world also destroys all query handles
  associated with it.
- Registering a type, creating or destroying an entity, and adding or removing
  a component are structural changes. They invalidate existing queries.
- Reading or replacing the bytes of an existing component is nonstructural and
  is allowed while iterating a query.
- Component removal uses swap-and-pop. Dense iteration order is not stable.
- Components are copied into and out of native storage. Component pointers are
  never exposed through the C API.

The C API serializes access through a process-wide mutex. Direct C++ use of
`EcsWorld`, `ComponentPool`, or `EcsQuery` is not thread-safe.

## Component layout

Version 1 components are fixed-size byte sequences. Registration records and
validates size and power-of-two alignment. Storage itself is byte-oriented
because all public access is copy-based; registered alignment is retained for
reflection and future typed/native-system storage.

## Mog byte-buffer bridge

Native package byte buffers map to `Array<u8>` through
`EXPR_PACKAGE_VALUE_BYTES`. Arguments are borrowed contiguous views valid only
during the callback. Results are copied immediately into a GC-owned Mog array.
Adding this value kind does not enlarge `ExprPackageValue`, so existing ABI v3
packages remain binary compatible.

## Mog native package

The low-level `github:ecs-native` package is a private dependency built to
`build/native/package.so` (or the platform module equivalent).
Its worlds and queries are opaque GC-managed handles; explicit destruction
closes them early, and their finalizers safely ignore already-closed or
world-invalidated handles.

`queryCreate` accepts component type IDs packed as consecutive little-endian
`u32` values in an `Array<u8>`. Component and query reads require the expected
byte size so the native runtime can reject incorrect layouts before copying.
Every C API failure becomes a Mog runtime error containing the operation name
and the native ECS error message.

Programs should import the ergonomic external source wrapper with
`const ecs = @import("github.com/moglang/ecs")`, create a world with
`ecs.CreateWorld()`, and use
its `component`, `create`, `add`, `get`, `set`, `remove`, `has`, and `query`
methods. `ComponentType` values remember their owning world and byte layout;
the wrapper rejects cross-world use and incorrect byte sizes before entering
native code. Queries provide `next`, `entity`, `get`, `set`, and `close`.

## Fixed-layout schemas and codecs

Mog structs are currently GC-managed class instances, so their in-memory
representation is deliberately not copied into components. The public ECS package instead
provides explicit `Schema` and `Field` values for safe fixed-layout data.
Schemas use natural field alignment, final tail padding, little-endian integer
and IEEE-754 float encodings, and one-byte booleans (`0` or `1`). Supported
fields are `i8/u8/i16/u16/i32/u32/i64/u64/f32/f64/bool/entity`; managed
strings, arrays, maps, closures, and objects cannot be added to a schema.

```mog
const ecs = @import("github.com/moglang/ecs")

var layout ecs.Schema = ecs.CreateSchema()
const x ecs.Field = layout.addF32("x")
const y ecs.Field = layout.addF32("y")

var world ecs.World = ecs.CreateWorld()
const position ecs.ComponentType =
    world.componentFromSchema("Position", layout)

var bytes Array<u8> = layout.buffer()
layout.putF32(bytes, x, 10.5f32)
layout.putF32(bytes, y, -4.0f32)

const entity u64 = world.create()
world.add(entity, position, bytes)
const stored Array<u8> = world.get(entity, position)
print(layout.getF32(stored, x))
```

Calling `size`, `alignment`, `buffer`, a codec method, or
`componentFromSchema` finalizes the schema. Adding fields after finalization is
an error. Codec methods verify the field's owning schema, exact field kind, and
the full buffer size before reading or writing.

## Distribution

The ECS repository publishes the public package and its platform-native private
dependency independently of Mog releases. The native module targets Linux
x86-64 GNU, Linux ARM64 GNU, and macOS ARM64. Building it requires CMake 3.16+
and a C++17 compiler.
