# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.3] - 2026-05-10

### Fixed

- SIGBUS handler no longer stores the previous handler's function
  pointer. `on_upgrade` re-ran `on_load`, which unconditionally
  re-installed the handler and saved iommap's own `sigbus_handler` as
  `original_sigbus_action`; the chain logic then recursed into itself
  on the next out-of-protected SIGBUS. Even with self-recursion fixed,
  storing the function pointer was unsafe across hot upgrade because
  the captured pointer could land in another iommap DSO image and
  dangle into freed text after `code:purge`. The handler now records
  only a category (`PRIOR_DFL`, `PRIOR_IGN`, or `PRIOR_OTHER`) and
  re-raises through a preinitialized `sigaction` template at signal
  time. Install is one-shot per DSO and a new `on_unload` callback
  restores `SIG_DFL` or `SIG_IGN` when iommap's handler is still
  active. Trade-offs (no chaining to a third-party SIGBUS handler, no
  third-party-handler restoration, single-DSO-lifetime `SIG_IGN`
  preservation) are documented in `guides/features.md`.
- `iommap_handle_alloc` no longer calls `pthread_rwlock_destroy` on a
  never-initialized lock when `pthread_rwlock_init` fails. A new
  `rwlock_initialized` flag gates the destroy in the handle
  destructor.

### Tests

- New `iommap_lifecycle_tests` module covers in-protected SIGBUS still
  being caught after the handler rewrite, and a basic round-trip after
  `code:load_file/1` (exercises the `on_upgrade` path).

## [1.1.2] - 2026-05-10

### Fixed

- SIGBUS handler no longer reads `__thread` storage on the signal
  path. `iommap_nif.so` is loaded via `dlopen`, so on FreeBSD (and
  any other platform whose dynamic loader allocates dlopened TLS
  lazily) reading a `__thread` variable from a thread that has not
  yet executed iommap code can call `__tls_get_addr`, which in turn
  may call `malloc` -- not async-signal-safe, leading to a crash on
  the next bus error. The protection state is now carried in a
  `pthread_key_t` slot allocated eagerly by iommap NIF entry points;
  the handler does a plain `pthread_getspecific` and treats a NULL
  slot as "not in a protected region, chain to the previous
  handler". This is the second half of the SIGBUS-handler fix
  started in 1.1.1; the crash only showed up when iommap was
  paired with another NIF in the same BEAM (enough TLS pressure
  to push iommap's slot out of the static-TLS reserve).

### Tests

- New `iommap_two_nif_tests` module reproduces the FreeBSD segfault
  inside iommap. A test-only helper NIF (`test/c_src/`) registers
  its own resource types alongside iommap; the test runs the
  region_binary load pattern in a tight loop, both inline and from
  a dedicated worker process. Without the TLS fix this reliably
  crashed BEAM on FreeBSD 14.2 and 14.4 in CI; with the fix, all
  matrix entries pass.

## [1.1.1] - 2026-05-10

### Fixed

- SIGBUS handler chains to the previously-installed handler when
  fired outside any iommap-protected region. The earlier handler
  always longjmped through a thread-local sigjmp_buf even when no
  sigsetjmp had run, which caused process segfaults when iommap was
  loaded alongside other NIFs that perform mmap I/O.
- `IOMMAP_MODE_WRITE` now opens the file `O_RDWR`. mmap with
  PROT_WRITE requires a readable fd; the previous O_WRONLY broke
  write-only mode at mmap time.
- `pread/3` and `region_binary/3` reject write-only handles with
  `eacces`, matching the symmetric guard already in `pwrite/3`.
- `pwrite/3` now wraps its memcpy in the same enter/leave protected
  pair as `pread/3` so a SIGBUS during pwrite is reported as
  `{error, sigbus}` rather than chained to the default handler.

### Tests

- New `read_close_then_parse` and `many_open_close_cycles` cases
  cover the read-only `open → region_binary → close → use binary`
  pattern that downstream callers rely on.
- FreeBSD CI now matrices 14.2 and 14.4.

## [1.1.0] - 2026-05-09

### Added

- `iommap:region_binary/3` returns a refcounted resource binary that
  points directly into the mapped region, with no data copy. Intended
  for hot zero-copy hand-off paths.

### Changed

- The NIF resource layout was split into a `mapping` resource (owns
  the mmap region and fd) and a `handle` resource (owns the BEAM
  handle term and one reference to the mapping). `close/1` releases
  the handle's reference to the mapping; `munmap` and `close(fd)` are
  deferred until any outstanding region binaries derived from that
  mapping are also garbage collected. Existing API behaviour is
  unchanged.
- `truncate/2` now allocates a fresh mapping (with a duplicated fd)
  and atomically swaps it into the handle. The previous mapping
  remains alive for outstanding region binaries.

### Notes

- `region_binary/3` is unsafe against external truncation that
  shrinks past a binary's range. Use `pread/3` when safety against
  external mutation is required.

## [1.0.0] - 2026-01-26

### Added

- Initial release of iommap
- Dirty NIF I/O schedulers for all I/O operations
- Cross-platform memory-mapped file I/O for Erlang/OTP
- Support for Linux, macOS, FreeBSD, and OpenBSD
- Core operations: `open/2,3`, `close/1`, `pread/3`, `pwrite/3`
- Synchronization: `sync/1,2` with sync/async modes
- File management: `truncate/2`, `position/1`
- Memory advice: `advise/4` with madvise hints
- Thread-safe implementation using pthread rwlocks
- SIGBUS protection for external file truncation
- Platform-specific optimizations:
  - `MAP_POPULATE` support on Linux
  - `MAP_NOCACHE` support on macOS
  - `fallocate` on Linux, `posix_fallocate` on BSD
- Comprehensive test suite with 20 tests
- CI pipeline for all supported platforms

[1.1.0]: https://github.com/benoitc/erlang-iommap/releases/tag/1.1.0
[1.0.0]: https://github.com/benoitc/erlang-iommap/releases/tag/1.0.0
