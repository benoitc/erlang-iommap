# Features & API Reference

## Features

### Core Operations
- **File-backed memory mapping** - Map files directly into memory for fast random access
- **Positional read/write** - Read and write at specific offsets without seeking
- **Synchronization** - Flush changes to disk with sync (blocking) or async modes
- **File resize** - Truncate or extend files with automatic remapping

### Access Modes
- `read` - Read-only access
- `write` - Write-only access
- `read_write` - Full access (default)

### Mapping Options
- `shared` - Changes are visible to other processes and written to file (default)
- `private` - Copy-on-write; changes are private to this process
- `lock` - Lock pages in memory to prevent swapping
- `populate` - Prefault pages on mapping (Linux only)
- `nocache` - Disable page caching (macOS only)
- `create` - Create file if it doesn't exist (returns `{error, einval}` in `read` mode)
- `truncate` - Truncate existing file (returns `{error, einval}` in `read` mode)
- `{size, N}` - Grow the file to N bytes if it is smaller. Never shrinks an existing file; combine with `truncate` to get exactly N bytes.

Files are opened with `O_CLOEXEC`, so the descriptor is not inherited by
child processes started with `open_port/2` or `os:cmd/1`.

### Memory Advice (madvise)
Provide hints to the kernel about access patterns:
- `normal` - No special treatment
- `random` - Expect random access pattern
- `sequential` - Expect sequential access pattern
- `willneed` - Will need these pages soon
- `dontneed` - Won't need these pages soon

## Platform Support

| Feature | Linux | macOS | FreeBSD | OpenBSD |
|---------|-------|-------|---------|---------|
| Basic mmap | Yes | Yes | Yes | Yes |
| MAP_POPULATE | Yes | No | No | No |
| MAP_NOCACHE | No | Yes | No | No |
| mlock | Yes | Yes | Yes | Yes |
| madvise | Yes | Yes | Yes | Yes |
| fallocate | Yes | ftruncate | posix_fallocate | ftruncate |

## API Reference

### open/2, open/3

```erlang
{ok, Handle} = iommap:open(Path, Options).
{ok, Handle} = iommap:open(Path, Mode, Options).
```

Opens a file for memory-mapped access.

**Arguments:**
- `Path` - File path (string or binary)
- `Mode` - Access mode: `read`, `write`, or `read_write`
- `Options` - List of options (see Mapping Options above)

**Returns:**
- `{ok, Handle}` on success
- `{error, Reason}` on failure

### close/1

```erlang
ok = iommap:close(Handle).
```

Closes the mapping and file descriptor. The handle becomes invalid after this call.

### pread/3

```erlang
{ok, Binary} = iommap:pread(Handle, Offset, Length).
```

Reads `Length` bytes starting at `Offset`. Returns a copy of the data.

**Arguments:**
- `Handle` - Memory map handle
- `Offset` - Byte offset to start reading
- `Length` - Number of bytes to read

**Returns:**
- `{ok, Binary}` containing the requested data
- `{error, out_of_bounds}` if range exceeds file size
- `{error, sigbus}` if memory access fault occurred

### pwrite/3

```erlang
ok = iommap:pwrite(Handle, Offset, Data).
```

Writes `Data` (binary or iolist) at `Offset`.

**Arguments:**
- `Handle` - Memory map handle
- `Offset` - Byte offset to start writing
- `Data` - Binary or iolist to write

**Returns:**
- `ok` on success
- `{error, out_of_bounds}` if range exceeds file size
- `{error, sigbus}` if memory access fault occurred

### sync/1, sync/2

```erlang
ok = iommap:sync(Handle).
ok = iommap:sync(Handle, Mode).
```

Flushes changes to disk.

**Arguments:**
- `Handle` - Memory map handle
- `Mode` - `sync` (blocking, default) or `async` (non-blocking)

### truncate/2

```erlang
ok = iommap:truncate(Handle, NewSize).
```

Resizes the file and remaps the memory region. Existing data beyond `NewSize` is lost.

**Arguments:**
- `Handle` - Memory map handle
- `NewSize` - New file size in bytes

### advise/4

```erlang
ok = iommap:advise(Handle, Offset, Length, Hint).
```

Provides access pattern hints to the kernel for optimization.

**Arguments:**
- `Handle` - Memory map handle
- `Offset` - Start of region
- `Length` - Length of region (0 for entire file)
- `Hint` - `normal`, `random`, `sequential`, `willneed`, or `dontneed`

### position/1

```erlang
{ok, Size} = iommap:position(Handle).
```

Returns the current size of the mapped region.

## Thread Safety

The NIF uses pthread read-write locks to ensure thread safety:
- Multiple concurrent reads are allowed
- Writes are exclusive
- Handle validity is checked under the lock

## Error Handling

Errors are returned as `{error, Reason}` tuples:

| Reason | Description |
|--------|-------------|
| `badarg` | Invalid arguments |
| `enomem` | Out of memory |
| `enoent` | File not found |
| `eacces` | Permission denied |
| `closed` | Handle already closed |
| `out_of_bounds` | Offset/length exceeds file size |
| `sigbus` | Memory access fault (file truncated externally) |

## SIGBUS Protection

The NIF installs a SIGBUS handler to protect against crashes when the underlying file is truncated externally while the mapping exists. If a SIGBUS occurs during read/write, `{error, sigbus}` is returned instead of crashing the VM.

### Limitations of the SIGBUS handler

To remain signal-safe across NIF hot upgrades, iommap deliberately does not store any function pointer for a pre-existing SIGBUS handler. As a consequence:

- **No chaining to a third-party SIGBUS handler.** When a SIGBUS fires outside iommap's protected region, iommap re-raises with the default action (typically a coredump and process termination). If another loaded NIF had installed its own SIGBUS handler before iommap, that library's recoverable SIGBUS inside its own protected region will reach iommap's handler first and terminate the VM. Co-existence between two SIGBUS-using NIFs in the same process is not solved.
- **No third-party handler restoration at unload.** When iommap is unloaded, only `SIG_DFL` or `SIG_IGN` is restored. A library that installed its own SIGBUS handler before iommap will need to reinstall after iommap is unloaded.
- **`SIG_IGN` preservation is single-DSO-lifetime only.** In the hot-upgrade path the new iommap DSO loads while the old DSO is still installed, so the new DSO sees the old DSO's handler and treats it as "other" rather than the original `SIG_IGN`. After the new DSO unloads, the original `SIG_IGN` is not restored — `SIG_DFL` is.

If you need to verify clean unload behaviour locally, a minimal recipe is:

```sh
erl -noshell -pa _build/default/lib/iommap/ebin -eval '
    {ok, _} = iommap:open("/tmp/probe.dat", read_write, [create, {size, 4096}]),
    halt(0).'
```

The VM exit triggers the unload callback. This is a clean-exit smoke; it does not externally observe the restored disposition.
