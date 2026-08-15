# iommap

[![Hex.pm](https://img.shields.io/hexpm/v/iommap.svg)](https://hex.pm/packages/iommap)
[![Hex Docs](https://img.shields.io/badge/hex-docs-blue.svg)](https://hexdocs.pm/iommap)
[![CI](https://github.com/benoitc/erlang-iommap/actions/workflows/ci.yml/badge.svg)](https://github.com/benoitc/erlang-iommap/actions/workflows/ci.yml)

Memory-mapped file I/O NIF for Erlang.

## Overview

`iommap` provides cross-platform memory-mapped file access for Erlang/OTP. Memory-mapped files allow applications to access file data as if it were in memory, enabling efficient random access patterns and shared memory between processes.

**Features:**

- Cross-platform: Linux, macOS, FreeBSD, OpenBSD
- Thread-safe with pthread read-write locks
- SIGBUS protection for safe memory access
- Positional read/write operations
- Memory advice (madvise) hints
- File resize with automatic remapping

## Installation

### Using Hex

Add to your `rebar.config`:

```erlang
{deps, [
    {iommap, "1.0.0"}
]}.
```

### From GitHub

```erlang
{deps, [
    {iommap, {git, "https://github.com/benoitc/erlang-iommap.git", {tag, "1.0.0"}}}
]}.
```

## Quick Start

```erlang
%% Create a new memory-mapped file
{ok, H} = iommap:open("/tmp/test.dat", read_write, [create, {size, 4096}]).

%% Write data at offset 0
ok = iommap:pwrite(H, 0, <<"Hello, iommap!">>).

%% Read data back
{ok, <<"Hello, iommap!">>} = iommap:pread(H, 0, 14).

%% Sync changes to disk
ok = iommap:sync(H).

%% Close the mapping
ok = iommap:close(H).
```

## API Reference

### Opening and Closing

```erlang
%% Open with default read_write mode
{ok, Handle} = iommap:open(Path, Options).

%% Open with explicit mode
{ok, Handle} = iommap:open(Path, Mode, Options).

%% Close handle
ok = iommap:close(Handle).
```

**Modes:** `read`, `write`, `read_write`

**Options:**

| Option | Description |
|--------|-------------|
| `{size, N}` | Grow the file to N bytes if it is smaller (required with `create`); never shrinks an existing file |
| `shared` | Changes visible to other processes (default) |
| `private` | Copy-on-write, changes are private |
| `lock` | Lock pages in memory (mlock) |
| `populate` | Prefault pages (Linux only) |
| `nocache` | Disable page caching (macOS only) |
| `create` | Create file if it doesn't exist (not allowed in `read` mode) |
| `truncate` | Truncate existing file (not allowed in `read` mode) |

### Reading and Writing

```erlang
%% Read Length bytes at Offset (copies)
{ok, Binary} = iommap:pread(Handle, Offset, Length).

%% Write Data at Offset
ok = iommap:pwrite(Handle, Offset, Data).
```

### Zero-Copy Region Binaries

```erlang
%% Refcounted view into the mapped region (no copy)
{ok, View} = iommap:region_binary(Handle, Offset, Length).
```

`region_binary/3` returns a binary whose underlying memory is the
page cache backing the mapping. No bytes are copied. The mapping is
kept alive for as long as the returned binary, or any sub-binary
derived from it, remains reachable.

The NIF uses two internal resources to support this lifetime: a
`mapping` resource owns the mmap region and file descriptor, and a
`handle` resource owns the BEAM-facing handle and holds one
reference to the mapping. `close/1` releases the handle's reference
to the mapping but does not call `munmap` if region binaries are
still outstanding. The `munmap` (and `close(fd)`) only run when the
last reference is dropped.

> Truncation hazard: `region_binary/3` is unsafe to use against
> files that may be truncated by external processes (or by
> `iommap:truncate/2` shrinking past the binary's range) while a
> returned binary is reachable. Reads of unmapped pages happen
> outside any NIF call and can crash the BEAM with SIGBUS. Callers
> needing safety against external mutation must use `pread/3` (which
> copies and is unaffected).

### Synchronization

```erlang
%% Sync to disk (blocking)
ok = iommap:sync(Handle).

%% Sync with mode: sync (blocking) or async (non-blocking)
ok = iommap:sync(Handle, async).
```

### File Operations

```erlang
%% Resize file and remap
ok = iommap:truncate(Handle, NewSize).

%% Get current mapped size
{ok, Size} = iommap:position(Handle).

%% Provide access pattern hints
ok = iommap:advise(Handle, Offset, Length, Hint).
```

**Hints:** `normal`, `random`, `sequential`, `willneed`, `dontneed`

## Documentation

- [Features & API Reference](guides/features.md)
- [Changelog](CHANGELOG.md)
- [HexDocs](https://hexdocs.pm/iommap)

Generate docs locally:

```bash
rebar3 ex_doc
```

## Building

```bash
# Compile
rebar3 compile

# Run tests
rebar3 eunit

# Type checking
rebar3 dialyzer

# Generate documentation
rebar3 ex_doc
```

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| Linux x86_64 | Tested | MAP_POPULATE supported |
| Linux ARM64 | Tested | MAP_POPULATE supported |
| macOS ARM64 | Tested | MAP_NOCACHE supported |
| FreeBSD | CI tested | posix_fallocate supported |
| OpenBSD | CI tested | |

## Support

Support, design and discussions are done via the [GitHub Tracker](https://github.com/benoitc/erlang-iommap/issues).

Professional support is available via [Enki Multimedia](https://enki-multimedia.eu). Contact sales@enki-multimedia.eu.

## License

MIT - See [LICENSE](LICENSE) for details.

Copyright (c) 2026 Benoit Chesneau
