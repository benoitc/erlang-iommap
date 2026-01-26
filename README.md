# iommap

Memory-mapped file I/O NIF for Erlang.

## Overview

`iommap` provides cross-platform memory-mapped file access for Erlang/OTP, compatible with Linux, macOS, and BSD systems.

## Installation

Add to your `rebar.config`:

```erlang
{deps, [
    {iommap, {git, "https://github.com/benoitc/erlang-iommap.git", {tag, "1.0.0"}}}
]}.
```

## Quick Start

```erlang
%% Create a new file with memory mapping
{ok, H} = iommap:open("/tmp/test.dat", read_write, [create, {size, 4096}]).

%% Write data
ok = iommap:pwrite(H, 0, <<"Hello, iommap!">>).

%% Read data back
{ok, <<"Hello, iommap!">>} = iommap:pread(H, 0, 14).

%% Sync to disk
ok = iommap:sync(H).

%% Close
ok = iommap:close(H).
```

## API

| Function | Description |
|----------|-------------|
| `open(Path, Options)` | Open with read_write mode |
| `open(Path, Mode, Options)` | Open file and create mmap |
| `close(Handle)` | Unmap and cleanup |
| `pread(Handle, Offset, Length)` | Read bytes at offset |
| `pwrite(Handle, Offset, Data)` | Write bytes at offset |
| `sync(Handle)` | Flush to disk (sync) |
| `sync(Handle, Mode)` | Flush with sync/async mode |
| `truncate(Handle, NewSize)` | Resize file and remap |
| `advise(Handle, Offset, Len, Hint)` | madvise hints |
| `position(Handle)` | Get file size |

See [doc/features.md](doc/features.md) for full documentation.

## Building

```bash
# Compile
rebar3 compile

# Run tests
rebar3 eunit

# Dialyzer
rebar3 dialyzer
```

## Docker Testing

Test on multiple Linux distributions:

```bash
# Test on Alpine Linux
make docker-test-alpine

# Test on Debian Linux
make docker-test-debian

# Test on all platforms
make docker-test
```

## Platform Support

- Linux (tested on Alpine, Debian, Ubuntu)
- macOS (tested on latest)
- FreeBSD (CI tested)
- OpenBSD (CI tested)

## License

Apache-2.0
