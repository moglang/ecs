# Native-Performance ECS Package: Implementation Status

This file began as the continuation handoff for the Mog ECS work. Phases 1-5
are complete as of 2026-08-01. Phase 6 is implemented but still needs a durable
baseline report/workflow, and Phase 7 remains partially complete. This file
records the implemented design, verification status, and remaining work so
completed work is not accidentally reimplemented.

## Repository boundary

The ECS is an external package repository under the `moglang` organization:

```text
/home/dev/Desktop/projects/moglang/packages/github.com/moglang/ecs
```

It is intentionally not part of the Mog language runtime. “Native” in this
plan means native C++ performance, not built-in runtime functionality. The Mog
runtime repository contains only the generic native-package ABI support needed
to exchange `Array<u8>` values with any native package. It must not compile,
link, test, install, or release ECS-specific sources or artifacts.

The following native work is complete and must not be reimplemented:

- Packed 64-bit entity IDs with 32-bit index and generation.
- Entity creation, destruction, index reuse, and stale-generation rejection.
- Per-world component registration and layout validation.
- Sparse-set component pools with copy-based add/read/write/remove.
- Swap-and-pop component removal.
- Entity destruction cleanup across all component pools.
- Structural world version tracking.
- Smallest-pool one-, two-, and three-component queries.
- Query iteration, component access, and structural invalidation.
- Generation-aware world and query handle tables.
- A validated, exception-safe C-compatible API.
- Mog native-package `Array<u8>` byte-buffer marshalling.
- C++ library, C API, query, churn, and sanitizer tests.

Important files:

```text
packages/github.com/moglang/ecs/lib/ecs/ecs_entity.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_entity_manager.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_entity_manager.cpp
packages/github.com/moglang/ecs/lib/ecs/ecs_component_type.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_component_pool.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_component_pool.cpp
packages/github.com/moglang/ecs/lib/ecs/ecs_world.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_world.cpp
packages/github.com/moglang/ecs/lib/ecs/ecs_query.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_query.cpp
packages/github.com/moglang/ecs/lib/ecs/ecs_handle_table.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_error.hpp
packages/github.com/moglang/ecs/lib/ecs/ecs_c_api.h
packages/github.com/moglang/ecs/lib/ecs/ecs_c_api.cpp
packages/github.com/moglang/ecs/lib/README.md
packages/github.com/moglang/ecs/tests/ecs_runtime_tests.cpp
```

The public/native packages, integration coverage, and benchmarks are in:

```text
packages/github.com/moglang/ecs/src/main.mog
packages/github.com/moglang/ecs/package.api.mog
packages/github.com/moglang/ecs/README.md
packages/github.com/moglang/ecs/native/src/package.cpp
packages/github.com/moglang/ecs/native/package.api.mog
packages/github.com/moglang/ecs/tests/test_ecs_package.sh
packages/github.com/moglang/ecs/tests/sample_ecs_codecs.mog
packages/github.com/moglang/ecs/tests/sample_ecs_language_matrix.mog
packages/github.com/moglang/ecs/benchmarks/ecs_runtime_benchmark.cpp
packages/github.com/moglang/ecs/benchmarks/bench_ecs.mog
```

The byte-buffer bridge is implemented in:

```text
mog/src/NativePackageAPI.hpp
mog/src/NativePackage.hpp
mog/src/NativePackage.cpp
mog/src/VirtualMachine.cpp
```

An integration example was added to the native math package:

```text
mog/packages/examples/math/package.cpp
mog/packages/examples/math/package.api.mog
mog/tests/sample_import_native_package.mog
```

## Settled design decisions

Keep these decisions unless a concrete implementation problem requires a
change:

1. Public native handles are generation-aware integers, never pointer casts.
2. Entity, world, and query handle value zero is invalid/reserved.
3. Component bytes use copy-in/copy-out access. No component pointer is exposed
   to Mog code.
4. Component registration validates nonempty names, nonzero sizes, and nonzero
   power-of-two alignments.
5. Registering an identical name/size/alignment is idempotent and returns the
   existing type ID. A conflicting layout returns `ComponentTypeConflict`.
6. Creating/destroying entities, registering new component types, and
   adding/removing components are structural changes.
7. Writing bytes to an existing component is nonstructural.
8. Structural changes invalidate active queries.
9. Dense component/query iteration order is unstable because removal uses
   swap-and-pop.
10. The C API serializes access through a process-wide mutex. Direct C++ ECS
    objects are not thread-safe.
11. Component pools use byte-oriented storage. Registered alignment is retained
    as metadata because public access is copy-based; aligned typed storage can
    be introduced later for native systems.
12. Mog byte buffers map to `Array<u8>` through
    `EXPR_PACKAGE_VALUE_BYTES`. Arguments are borrowed only during a callback;
    results are copied immediately into GC-owned arrays.
13. The byte-buffer addition intentionally keeps native package ABI version 3.
    Its union member does not enlarge `ExprPackageValue`, preserving binary
    layout compatibility with existing ABI-3 packages.

## Goal status

In progress. The external native library, public `github.com/moglang/ecs` API, fixed-layout schemas,
and language integration coverage are complete. Benchmark implementations,
Linux installation packaging, and documentation exist, but Phase 6 baseline
recording and Phase 7 cross-platform/distribution validation remain.

## Completion summary (2026-08-01)

- Phase 1 is complete: the external repository builds `mog_ecs` as a
  PIC static library for its package module and tests. The Mog interpreter does
  not link it.
- Phase 2 is complete: internal `github:ecs-native` exposes opaque world/query
  handles and all planned entity, component, and query operations through the
  native ABI.
- End-to-end Mog tests cover byte component operations, query reads/writes,
  explicit destruction/finalization, and structural query invalidation.
- Phase 3 is complete: the public `github.com/moglang/ecs` source package provides `World`,
  `ComponentType`, and `Query`, validates ownership and component buffer sizes,
  and delegates to the private `github:ecs-native` package in the same repository.
- Its Mog-only integration test creates 10,000 Position/Velocity entities,
  visits every match once, and performs query writes without invalidation.
- Phases 4 and 5 are complete via the explicit-schema fallback and expanded
  integration tests described below.
- Phase 4 uses the safe explicit-schema path. `github.com/moglang/ecs` now exposes `Schema`
  and `Field`, deterministic natural alignment and tail padding, and
  little-endian codecs for every planned integer width, `f32`, `f64`,
  canonical one-byte booleans, and entity IDs. Managed values remain
  unrepresentable by construction.
- Schema codecs validate schema ownership, field kind, and full buffer size.
  A Position `{f32 x, f32 y}` and the full scalar matrix round-trip through
  native ECS storage in automated tests.
- Phase 5 language coverage now includes one-, two-, and three-component
  queries, empty results, nonstructural writes, structural invalidation,
  removal, stale entity generations after reuse, wrong sizes, wrong-world
  component use, explicit closure/finalization, and byte buffers containing
  both `0xff` and zero bytes (including an empty native round trip).
- Phase 6 benchmark workloads live in the external repository under
  `benchmarks/ecs_runtime_benchmark.cpp`
  (separate direct-C++ and C API timings) and `benchmarks/bench_ecs.mog`
  (Mog/native call overhead and full interpreted loops). They cover entity
  counts of 1,000, 10,000, and 100,000 without adding speculative runtime
  optimizations. Timings were observed during verification but have not yet
  been committed as a durable baseline report or automated workflow.
- Phase 7 no longer installs either ECS package with the Mog runtime. The
  external repository owns package builds and distribution. ECS documentation
  includes a Position tutorial,
  layout/ownership/lifetime rules, unstable query order, invalidation,
  declared platform targets, and native build requirements. Registry handling
  and non-host platform packaging still require confirmation.

The implemented public API supports this experience:

```mog
const ecs = @import("github.com/moglang/ecs")

var positionLayout ecs.Schema = ecs.CreateSchema()
const x ecs.Field = positionLayout.addF32("x")
const y ecs.Field = positionLayout.addF32("y")

var world ecs.World = ecs.CreateWorld()
const position ecs.ComponentType =
    world.componentFromSchema("Position", positionLayout)
const velocity ecs.ComponentType = world.component("Velocity", 8u64, 4u64)

var positionBytes Array<u8> = positionLayout.buffer()
positionLayout.putF32(positionBytes, x, 10.0f32)
positionLayout.putF32(positionBytes, y, 20.0f32)

const entity u64 = world.create()
world.add(entity, position, positionBytes)
world.add(entity, velocity, [0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8, 0u8])

var query ecs.Query = world.query([position, velocity])
while (query.next()) {
    var bytes Array<u8> = query.get(position)
    positionLayout.putF32(
        bytes,
        x,
        positionLayout.getF32(bytes, x) + 1.0f32
    )
    query.set(position, bytes)
}
query.close()
world.close()
```

The public wrapper intentionally uses explicit iteration and explicit
fixed-layout codecs; no new language syntax was introduced.

## Phase 1: Package/build architecture — complete

All ECS sources, package manifests, tests, and benchmarks live in the external
`moglang/ecs` repository. Its CMake build creates a dedicated PIC
`mog_ecs` static library and links that library only into the external
native package module and external tests.

Implemented architecture:

1. Create a dedicated `mog_ecs` static library from the ECS `.cpp`
   files.
2. Enable position-independent code for that target so a native package module
   can link it.
3. Keep ECS sources out of the Mog runtime repository and `interpreter_core`.
4. Link external `ecs_runtime_tests` directly to `mog_ecs`.
5. Link the external native package module to `mog_ecs`.

It is acceptable for the package module to contain its own ECS C API handle
tables. That gives the package a self-contained runtime. Do not rely on
unexported symbols from the interpreter executable.

Completion criteria (met):

- The Mog runtime builds with no ECS source, target, test, package staging, or
  install rule.
- The external package build succeeds independently.
- `ecs_runtime_tests` links against the dedicated target.
- The package module loads without unresolved symbols.
- ASan/UBSan flags continue to reach the runtime, test, and package targets.
- Strict ECS warnings remain enabled:
  `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`.

## Phase 2: Low-level native ECS package — complete

The internal first-class native package is implemented under:

```text
packages/github.com/moglang/ecs/native/
```

It follows the repository's native-package structure:

```text
mog.toml
package.toml (if required by existing conventions)
package.api.mog
NativePackageAPI.hpp or the repository-standard include strategy
src/package.cpp
```

It uses namespace `github`, package name `ecs-native`, import name `ecs_native`,
and native package ABI version 3. The public `ecs` import is supplied by the
Phase 3 source wrapper.

Expose opaque GC-managed handle types for worlds and queries. Native package
handle payloads may contain C API integer handles, but Mog programs must not see
native addresses.

Minimum native exports:

```text
worldCreate() -> WorldHandle
worldDestroy(WorldHandle) -> void

entityCreate(WorldHandle) -> u64
entityDestroy(WorldHandle, u64) -> void
entityIsAlive(WorldHandle, u64) -> bool

componentRegister(WorldHandle, str, u64, u64) -> u64
componentAdd(WorldHandle, u64, u64, Array<u8>) -> void
componentRemove(WorldHandle, u64, u64) -> void
componentHas(WorldHandle, u64, u64) -> bool
componentRead(WorldHandle, u64, u64, u64) -> Array<u8>
componentWrite(WorldHandle, u64, u64, Array<u8>) -> void

queryCreate(WorldHandle, Array<u8>) -> QueryHandle
queryNext(QueryHandle) -> bool
queryEntity(QueryHandle) -> u64
queryRead(QueryHandle, u64, u64) -> Array<u8>
queryWrite(QueryHandle, u64, Array<u8>) -> void
queryDestroy(QueryHandle) -> void
```

`queryCreate` may initially accept component type IDs encoded as consecutive
little-endian `u32` values in `Array<u8>`. If the native ABI is extended to
support `Array<u64>` or `Array<u32>` directly, do so only with the same ABI
layout and ownership care used for byte buffers.

Native handle finalizers must be idempotent:

- Finalizing a live world destroys its C API world handle.
- Explicit `worldDestroy` marks the payload closed so finalization does not
  destroy it twice.
- Finalizing a live query destroys its query handle.
- Destroying a world already invalidates all associated query handles; later
  query finalization must tolerate `InvalidQuery`.

All C API errors must become clear Mog runtime errors. Include the operation and
`ecs_last_error_message()` text. Check numeric narrowing before converting Mog
`u64` values to `u32` sizes, alignments, or component IDs.

Completion criteria (met):

- A Mog program can create/destroy a world and entity.
- Invalid/stale handles produce a language-visible error.
- A byte component can be registered, added, read, written, tested, and
  removed.
- GC finalization releases world/query handles exactly once.
- A structural change makes `queryNext` fail with an invalidation error.
- A query component write does not invalidate the query.

## Phase 3: Mog-facing ergonomic API — complete

The public source-level `github.com/moglang/ecs` package wraps the private
`github:ecs-native` dependency from the same external repository, so users do
not work directly with the low-level functional API.

Target language objects:

```text
World
ComponentType
Query
Entity (may remain a u64 alias in version 1)
```

Desired responsibilities:

### `World`

- Own a native world handle.
- `create()` and `destroy(entity)`.
- `isAlive(entity)`.
- Register components and remember their byte size/alignment.
- Validate that buffers supplied to add/set match the component size before
  crossing the native boundary.
- Create queries from one or more `ComponentType` values.

### `ComponentType`

- Store native type ID, name, size, alignment, and owning world identity.
- Reject use with another world.
- Make component read sizes implicit so callers do not repeatedly pass them.

### `Query`

- Own a native query handle.
- Provide explicit `next()`, `entity()`, `get(componentType)`,
  `set(componentType, bytes)`, and `close()` methods.
- Reject component types that belong to another world.
- Document structural invalidation clearly.

Prefer explicit iteration for the first release. Add `for` iteration protocol
support only if Mog already has a stable custom-iterator protocol.

Completion criteria (met):

- A Mog-only integration test creates at least 10,000 entities with Position
  and Velocity byte components.
- A two-component query visits every matching entity exactly once.
- Query writes update every Position without invalidating iteration.
- World/component/query ownership errors are understandable.
- Explicit close and GC finalization are both safe.

## Phase 4: Fixed-layout value serialization — complete

The explicit codec/schema fallback was selected because Mog structs are
GC-managed class instances without a stable fixed-layout representation.
`Schema` and `Field` provide deterministic layout without exposing managed
runtime memory.

Frontend/runtime inspection found width-aware numeric types but no stable value
layout or safe reflection suitable for copying ordinary structs into native
storage. The two evaluated paths were:

### Deferred path: explicit fixed-layout structs

This path was not selected because it would require invasive compiler and
runtime changes to normal class semantics:

- Add a fixed-layout value declaration or annotation.
- Calculate deterministic size, alignment, and field offsets.
- Restrict fields recursively to trivially copyable fixed-layout types.
- Expose layout metadata to the ECS wrapper.
- Serialize values to `Array<u8>` and reconstruct them on reads.
- Specify byte order and boolean representation.

### Implemented path: explicit codecs/schemas

ECS storage remains generic and the wrapper provides explicit little-endian
byte codecs for supported primitives:

```text
i8/u8/i16/u16/i32/u32/i64/u64/f32/f64/bool/entity
```

The source-level schema calculates offsets and encodes/decodes byte arrays.
This preserves runtime safety and unblocks real ECS use without exposing GC
representations.

Do not serialize managed strings, maps, arrays, closures, class instances, or
GC pointers directly into component storage.

Completion criteria (met):

- Position `{f32 x, f32 y}` round-trips without corruption.
- Nested fixed-layout values work if the preferred path is selected.
- Incorrect buffer sizes and unsupported field types fail clearly.
- Encoding is deterministic across repeated runs.
- Tests cover signed/unsigned integers, floats, booleans, and entity IDs.

## Phase 5: Language integration tests — complete

Successful and failing `.mog` programs now cover:

- World/entity lifetime.
- Component registration.
- Add/read/write/remove/has.
- One-, two-, and three-component queries.
- Empty query results.
- Structural invalidation.
- Nonstructural query writes.
- Stale entities after index reuse.
- Wrong component sizes.
- Component types used with the wrong world.
- Explicit destruction plus GC finalization.
- Native byte-buffer input and output, including empty buffers and `0xff`.

They run through the existing shell-test/CTest conventions rather than relying
on manual execution.

## Phase 6: Benchmarks — in progress

The benchmark suite contains separate measurements for:

1. Pure C++ runtime operations.
2. C API boundary overhead.
3. Mog-to-native call overhead.
4. Full interpreted ECS loops.

Covered workloads:

- Entity create/destroy.
- Component add/remove/read/write.
- One- and two-component queries.
- 1,000, 10,000, and 100,000 entities.
- Query copy-in/copy-out update loop.

The workloads run and emit timings, but baseline numbers currently exist only
in transient command output. Before completing this phase:

- Run the suite in a documented release/profiling configuration.
- Commit a dated baseline report containing environment/build details and the
  four boundary measurements.
- Provide a repeatable benchmark command or script that runs both the native
  and Mog suites without mixing them into correctness CI.

Do not add query caching, archetypes, native systems, or bulk APIs until the
recorded profiling data identifies a bottleneck.

## Phase 7: Distribution and documentation — in progress

Completed:

- ECS code and artifacts have been removed from Mog runtime builds, tests,
  installation, and CPack contents.
- The public wrapper, private native binding, C++ library, tests, benchmarks,
  and documentation are colocated in the external `moglang/ecs` repository.
- Source/native package manifests and API metadata are present.
- Platform targets and native build requirements are documented.
- A minimal Position/Velocity schema tutorial is documented.
- Unstable iteration order, query invalidation, handle lifetime, component
  restrictions, and byte-buffer ownership are documented.
- The former built-in Linux x86-64 layout was exercised before extraction.

Remaining before completion:

- Add independent package CI and releases for the external repository.
- Publish the public source package and its private native dependency through
  the Mog registry without adding either artifact to Mog runtime releases.
- Build and verify the Linux ARM64 package layout.
- Build and verify the macOS ARM64 module name, install layout, package loading,
  and CPack contents rather than relying only on manifest declarations.
- Confirm the external repository's release automation publishes ECS package
  artifacts for every supported target.

## Required verification before each handoff

Use isolated build directories if the repository's existing `build/` cache was
created from another checkout path.

Normal verification:

```bash
cmake -S mog -B /tmp/moglang-runtime-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/moglang-runtime-build -j2

cmake -S packages/github.com/moglang/ecs -B /tmp/moglang-ecs-build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMOG_EXECUTABLE=/tmp/moglang-runtime-build/mog
cmake --build /tmp/moglang-ecs-build -j2
ctest --test-dir /tmp/moglang-ecs-build --output-on-failure
```

Sanitizer verification:

```bash
cmake -S packages/github.com/moglang/ecs -B /tmp/moglang-ecs-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_ASAN=ON \
  -DENABLE_UBSAN=ON
cmake --build /tmp/moglang-ecs-sanitize --target ecs_runtime_tests -j2
ASAN_OPTIONS=detect_leaks=0 /tmp/moglang-ecs-sanitize/ecs_runtime_tests
```

LeakSanitizer cannot run under the managed ptraced environment, so
`detect_leaks=0` is expected there. ASan bounds checks and UBSan must still run.

Also run:

```bash
git diff --check
```

Compile `ecs_c_api.h` from a C11 translation unit with strict warnings whenever
the public C API changes.

## Current verified state after extraction (2026-08-01)

The completed implementation passed:

- A fresh Mog runtime CMake build with no ECS target, source, test, staging, or
  install rule.
- An independent external ECS CMake build.
- The external CTest suite, including `ecs_runtime_tests` and the Mog package
  integration suite, passes against that freshly built runtime.
- Combined ASan/UBSan runtime and Mog package integration tests with
  `ASAN_OPTIONS=detect_leaks=0`.
- Pure-C compilation of `ecs_c_api.h`.
- Native package imports, explicit closure, and native handle finalization.
- `Array<u8>` native round trips for `[0, 1, 255]` and `[]`.
- Schema round trips for Position `{f32 x, f32 y}`, all signed and unsigned
  integer widths, `f32`, `f64`, canonical booleans, and entity IDs.
- Direct C++, C API, Mog/native-boundary, and full interpreted ECS benchmark
  workloads at 1,000, 10,000, and 100,000 entities.
- Managed-project imports of `github.com/moglang/ecs`, with the private native
  dependency staged independently from the runtime.
- Earlier built-in install/CPack verification is historical only; ECS is no
  longer included in Mog runtime installation or CPack output.
- `git diff --check`.

Phases 6 and 7 retain the specific remaining work listed above. Runtime
optimization should begin only after the durable Phase 6 baseline exists;
query caching, archetypes, native systems, bulk APIs, and fixed-layout compiler
syntax remain outside the current implementation scope.
