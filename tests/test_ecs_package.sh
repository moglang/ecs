#!/bin/bash
set -eu

INTERPRETER="$1"
REPOSITORY="$2"
BUILD_DIR="$3"
TEST_DIR="$REPOSITORY/tests"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
export MOG_CACHE_DIR="$WORK_DIR/cache"

STAGE="$WORK_DIR/github.com/moglang/ecs"
NATIVE_STAGE="$WORK_DIR/github/ecs-native"
PROJECT="$WORK_DIR/project"
mkdir -p "$STAGE" "$NATIVE_STAGE" "$PROJECT"
cp -R "$REPOSITORY/." "$STAGE/"
cp -R "$REPOSITORY/native/." "$NATIVE_STAGE/"
sed -i.bak \
    "s|ecs_native = { package = \"github:ecs-native\", version = \"0.1.0\" }|ecs_native = { path = \"$NATIVE_STAGE\", package = \"github:ecs-native\", version = \"0.1.0\" }|" \
    "$STAGE/mog.toml"
rm -f "$STAGE/mog.toml.bak"
if [[ "$(uname -s)" == "Darwin" ]]; then
    cp "$BUILD_DIR/native/package.so" "$NATIVE_STAGE/package.dylib"
else
    cp "$BUILD_DIR/native/package.so" "$NATIVE_STAGE/package.so"
fi

"$INTERPRETER" validate-package "$NATIVE_STAGE"
"$INTERPRETER" validate-package "$STAGE"

printf '%s\n' \
    'kind = "project"' \
    'name = "ecs-package-tests"' \
    'version = "0.0.0"' \
    '' \
    '[dependencies]' \
    "\"github.com/moglang/ecs\" = { path = \"$STAGE\", version = \"0.1.0\" }" \
    "ecs_native = { path = \"$NATIVE_STAGE\", version = \"0.1.0\" }" \
    > "$PROJECT/mog.toml"

cd "$PROJECT"

SUCCESS_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_import_native_ecs.mog" 2>&1)" || {
    printf '%s\n' "$SUCCESS_OUTPUT"
    exit 1
}

if [[ "$SUCCESS_OUTPUT" != *"ecs_native_ok"* ]]; then
    printf 'missing ECS success marker:\n%s\n' "$SUCCESS_OUTPUT"
    exit 1
fi

set +e
INVALIDATION_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_import_native_ecs_invalidation.mog" 2>&1)"
INVALIDATION_STATUS=$?
set -e

if [[ $INVALIDATION_STATUS -eq 0 ]] ||
   [[ "$INVALIDATION_OUTPUT" != *"queryNext: query was invalidated by a structural world change"* ]]; then
    printf 'expected language-visible query invalidation error:\n%s\n' "$INVALIDATION_OUTPUT"
    exit 1
fi

WRAPPER_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_wrapper.mog" 2>&1)" || {
    printf '%s\n' "$WRAPPER_OUTPUT"
    exit 1
}

if [[ "$WRAPPER_OUTPUT" != *"ecs_wrapper_ok"* ]]; then
    printf 'missing ECS wrapper success marker:\n%s\n' "$WRAPPER_OUTPUT"
    exit 1
fi

CODEC_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_codecs.mog" 2>&1)" || {
    printf '%s\n' "$CODEC_OUTPUT"
    exit 1
}

if [[ "$CODEC_OUTPUT" != *"ecs_codecs_ok"* ]]; then
    printf 'missing ECS codec success marker:\n%s\n' "$CODEC_OUTPUT"
    exit 1
fi

MATRIX_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_language_matrix.mog" 2>&1)" || {
    printf '%s\n' "$MATRIX_OUTPUT"
    exit 1
}

if [[ "$MATRIX_OUTPUT" != *"ecs_language_matrix_ok"* ]]; then
    printf 'missing ECS language matrix success marker:\n%s\n' "$MATRIX_OUTPUT"
    exit 1
fi

set +e
OWNERSHIP_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_wrapper_wrong_world.mog" 2>&1)"
OWNERSHIP_STATUS=$?
set -e

if [[ $OWNERSHIP_STATUS -eq 0 ]] ||
   [[ "$OWNERSHIP_OUTPUT" != *"ECS component type belongs to a different world"* ]]; then
    printf 'expected ECS wrapper ownership error:\n%s\n' "$OWNERSHIP_OUTPUT"
    exit 1
fi

set +e
SIZE_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_wrapper_wrong_size.mog" 2>&1)"
SIZE_STATUS=$?
set -e

if [[ $SIZE_STATUS -eq 0 ]] ||
   [[ "$SIZE_OUTPUT" != *"ECS component byte buffer has the wrong size"* ]]; then
    printf 'expected ECS wrapper component-size error:\n%s\n' "$SIZE_OUTPUT"
    exit 1
fi

set +e
SCHEMA_SIZE_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_schema_wrong_buffer.mog" 2>&1)"
SCHEMA_SIZE_STATUS=$?
set -e

if [[ $SCHEMA_SIZE_STATUS -eq 0 ]] ||
   [[ "$SCHEMA_SIZE_OUTPUT" != *"ECS schema byte buffer has the wrong size"* ]]; then
    printf 'expected ECS schema buffer-size error:\n%s\n' "$SCHEMA_SIZE_OUTPUT"
    exit 1
fi

set +e
UNSUPPORTED_OUTPUT="$($INTERPRETER "$TEST_DIR/sample_ecs_schema_unsupported.mog" 2>&1)"
UNSUPPORTED_STATUS=$?
set -e

if [[ $UNSUPPORTED_STATUS -eq 0 ]] ||
   [[ "$UNSUPPORTED_OUTPUT" != *"ECS schema field type is unsupported"* ]]; then
    printf 'expected ECS unsupported-field error:\n%s\n' "$UNSUPPORTED_OUTPUT"
    exit 1
fi

printf 'ecs native package tests passed\n'
