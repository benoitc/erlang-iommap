# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
