# iommap - Erlang Memory-Mapped File I/O

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
- `create` - Create file if it doesn't exist
- `truncate` - Truncate existing file

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

### close/1

```erlang
ok = iommap:close(Handle).
```

Closes the mapping and file descriptor. The handle becomes invalid.

### pread/3

```erlang
{ok, Binary} = iommap:pread(Handle, Offset, Length).
```

Reads `Length` bytes starting at `Offset`. Returns a copy of the data.

### pwrite/3

```erlang
ok = iommap:pwrite(Handle, Offset, Data).
```

Writes `Data` (binary or iolist) at `Offset`.

### sync/1, sync/2

```erlang
ok = iommap:sync(Handle).
ok = iommap:sync(Handle, Mode).
```

Flushes changes to disk. Mode can be `sync` (blocking) or `async`.

### truncate/2

```erlang
ok = iommap:truncate(Handle, NewSize).
```

Resizes the file and remaps the memory region.

### advise/4

```erlang
ok = iommap:advise(Handle, Offset, Length, Hint).
```

Provides access pattern hints to the kernel.

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
- `badarg` - Invalid arguments
- `enomem` - Out of memory
- `enoent` - File not found
- `eacces` - Permission denied
- `closed` - Handle already closed
- `out_of_bounds` - Offset/length exceeds file size
- `sigbus` - Memory access fault (file truncated externally)

## SIGBUS Protection

The NIF installs a SIGBUS handler to protect against crashes when the underlying file is truncated externally while the mapping exists. If a SIGBUS occurs during read/write, `{error, sigbus}` is returned instead of crashing the VM.
