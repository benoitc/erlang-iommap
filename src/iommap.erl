%%% @author Benoit Chesneau <bchesneau@gmail.com>
%%% @copyright 2026 Benoit Chesneau
%%% @doc Memory-mapped file I/O for Erlang.
%%%
%%% This module provides cross-platform memory-mapped file access
%%% compatible with Linux, macOS, FreeBSD, and OpenBSD.
%%%
%%% Memory-mapped files allow applications to access file data as if it
%%% were in memory, enabling efficient random access patterns and shared
%%% memory between processes.
%%%
%%% == Quick Start ==
%%%
%%% ```
%%% %% Create and write to a memory-mapped file
%%% {ok, H} = iommap:open("/tmp/test.dat", read_write, [create, {size, 4096}]),
%%% ok = iommap:pwrite(H, 0, <<"Hello, iommap!">>),
%%% {ok, <<"Hello, iommap!">>} = iommap:pread(H, 0, 14),
%%% ok = iommap:sync(H),
%%% ok = iommap:close(H).
%%% '''
%%%
%%% == Access Modes ==
%%%
%%% <ul>
%%%   <li>`read' - Read-only access to the file</li>
%%%   <li>`write' - Write-only access to the file</li>
%%%   <li>`read_write' - Full read and write access (default)</li>
%%% </ul>
%%%
%%% == Mapping Options ==
%%%
%%% <ul>
%%%   <li>`shared' - Changes are visible to other processes (default)</li>
%%%   <li>`private' - Copy-on-write; changes are private</li>
%%%   <li>`lock' - Lock pages in memory (mlock)</li>
%%%   <li>`populate' - Prefault pages on mapping (Linux only)</li>
%%%   <li>`nocache' - Disable page caching (macOS only)</li>
%%%   <li>`create' - Create file if it doesn't exist</li>
%%%   <li>`truncate' - Truncate existing file</li>
%%%   <li>`{size, N}' - Initial size for new files</li>
%%% </ul>
%%%
%%% == Thread Safety ==
%%%
%%% All operations are thread-safe. The NIF uses pthread read-write locks
%%% to allow multiple concurrent reads while writes are exclusive.
%%%
%%% == Error Handling ==
%%%
%%% Operations return `{error, Reason}' on failure. Common reasons:
%%% <ul>
%%%   <li>`badarg' - Invalid arguments</li>
%%%   <li>`enomem' - Out of memory</li>
%%%   <li>`enoent' - File not found</li>
%%%   <li>`eacces' - Permission denied</li>
%%%   <li>`closed' - Handle already closed</li>
%%%   <li>`out_of_bounds' - Offset/length exceeds file size</li>
%%%   <li>`sigbus' - Memory access fault (file truncated externally)</li>
%%% </ul>
%%%
%%% == Zero-Copy Region Binaries ==
%%%
%%% `region_binary/3' returns a refcounted binary that points directly
%%% into the mapped region with no data copy. The underlying mapping
%%% is kept alive (its `munmap' is deferred) for as long as any such
%%% binary, or any sub-binary derived from it, is reachable.
%%%
%%% Lifetime model: the NIF uses two resources internally. The handle
%%% holds one reference to the mapping; `close/1' releases that
%%% reference but does not call `munmap' if region binaries are still
%%% outstanding. The mapping is unmapped only when the last reference
%%% (handle ref + outstanding region binaries) is dropped.
%%%
%%% Truncation hazard: `region_binary/3' is unsafe to use against
%%% files that may be truncated by external processes (or by
%%% `iommap:truncate/2' shrinking past the binary's range) while a
%%% returned binary is reachable. Reads of unmapped pages happen
%%% outside any NIF call and can crash the BEAM with SIGBUS. Callers
%%% needing safety against external mutation must use `pread/3'
%%% (which copies and is unaffected).
%%%
%%% @end
-module(iommap).

-export([open/2, open/3, close/1]).
-export([pread/3, pwrite/3]).
-export([region_binary/3]).
-export([sync/1, sync/2]).
-export([truncate/2]).
-export([advise/4]).
-export([position/1]).

-on_load(init/0).

-define(NIF_NOT_LOADED, erlang:nif_error(nif_not_loaded)).

%% Types
-type handle() :: reference().
-type mode() :: read | write | read_write.
-type sync_mode() :: sync | async.
-type advise_hint() :: normal | random | sequential | willneed | dontneed.

-type open_option() ::
    read | write | read_write |
    {size, non_neg_integer()} |
    shared | private |
    lock |
    populate |
    nocache |
    create |
    truncate.

-export_type([handle/0, mode/0, sync_mode/0, advise_hint/0, open_option/0]).

%% @doc Initialize NIF.
-spec init() -> ok | {error, term()}.
init() ->
    PrivDir = case code:priv_dir(iommap) of
        {error, bad_name} ->
            AppFile = filename:dirname(code:which(?MODULE)),
            filename:join(filename:dirname(AppFile), "priv");
        Dir ->
            Dir
    end,
    SoName = filename:join(PrivDir, "iommap_nif"),
    erlang:load_nif(SoName, 0).

%% @doc Open a file for memory-mapped access with default read_write mode.
%% @equiv open(Path, read_write, Options)
-spec open(Path, Options) -> {ok, handle()} | {error, term()} when
    Path :: file:filename_all(),
    Options :: [open_option()].
open(Path, Options) ->
    open(Path, read_write, Options).

%% @doc Open a file for memory-mapped access.
%%
%% Opens the file at `Path' with the given `Mode' and creates a memory mapping.
%%
%% == Options ==
%% <ul>
%%   <li>`{size, N}' - Initial size for new files (required with `create')</li>
%%   <li>`shared' - Use MAP_SHARED (default)</li>
%%   <li>`private' - Use MAP_PRIVATE (copy-on-write)</li>
%%   <li>`lock' - Lock pages in memory (mlock)</li>
%%   <li>`populate' - Prefault pages (Linux only)</li>
%%   <li>`nocache' - Disable caching (macOS only)</li>
%%   <li>`create' - Create file if it doesn't exist</li>
%%   <li>`truncate' - Truncate existing file</li>
%% </ul>
-spec open(Path, Mode, Options) -> {ok, handle()} | {error, term()} when
    Path :: file:filename_all(),
    Mode :: mode(),
    Options :: [open_option()].
open(Path, Mode, Options) when is_binary(Path) ->
    nif_open(Path, Mode, Options);
open(Path, Mode, Options) when is_list(Path) ->
    %% Encode charlists (which may contain code points above 255) in
    %% the native filename encoding instead of iolist_to_binary/1,
    %% which raises on such input.
    case unicode:characters_to_binary(Path, unicode, file:native_name_encoding()) of
        PathBin when is_binary(PathBin) ->
            nif_open(PathBin, Mode, Options);
        _ ->
            {error, badarg}
    end;
open(_, _, _) ->
    {error, badarg}.

%% @doc Close a memory-mapped file handle.
%%
%% Unmaps the memory region and closes the file descriptor.
%% The handle becomes invalid after this call.
-spec close(Handle) -> ok | {error, term()} when
    Handle :: handle().
close(Handle) ->
    nif_close(Handle).

%% @doc Read bytes from a memory-mapped file at the given offset.
%%
%% Returns a new binary containing the requested bytes.
%% The binary is a copy of the mapped memory.
-spec pread(Handle, Offset, Length) -> {ok, binary()} | {error, term()} when
    Handle :: handle(),
    Offset :: non_neg_integer(),
    Length :: non_neg_integer().
pread(Handle, Offset, Length) when Offset >= 0, Length >= 0 ->
    nif_pread(Handle, Offset, Length);
pread(_, _, _) ->
    {error, badarg}.

%% @doc Write data to a memory-mapped file at the given offset.
%%
%% Writes the binary data to the mapped region starting at `Offset'.
-spec pwrite(Handle, Offset, Data) -> ok | {error, term()} when
    Handle :: handle(),
    Offset :: non_neg_integer(),
    Data :: iodata().
pwrite(Handle, Offset, Data) when Offset >= 0 ->
    Bin = iolist_to_binary(Data),
    nif_pwrite(Handle, Offset, Bin);
pwrite(_, _, _) ->
    {error, badarg}.

%% @doc Return a zero-copy refcounted binary view into the mapped region.
%%
%% Unlike `pread/3', no bytes are copied: the returned binary is a
%% resource binary whose underlying memory is the page-cache backing
%% the mapping. The mapping is kept alive for as long as the returned
%% binary (or any sub-binary derived from it) remains reachable.
%%
%% This primitive is intended for hot zero-copy hand-off paths, e.g.
%% passing the bytes to another NIF as `ErlNifBinary' without going
%% through the BEAM heap.
%%
%% Reads of the returned binary occur outside any NIF call. If the
%% underlying file is truncated (by an external process, or by
%% `truncate/2' shrinking past the binary's range) while the binary
%% is reachable, accessing it can crash the BEAM with SIGBUS. Use
%% `pread/3' if safety against external mutation is required.
-spec region_binary(Handle, Offset, Length) ->
        {ok, binary()} | {error, Reason} when
    Handle :: handle(),
    Offset :: non_neg_integer(),
    Length :: non_neg_integer(),
    Reason :: badarg | closed | out_of_bounds.
region_binary(Handle, Offset, Length) when Offset >= 0, Length >= 0 ->
    nif_region_binary(Handle, Offset, Length);
region_binary(_, _, _) ->
    {error, badarg}.

%% @doc Synchronize the memory mapping with the underlying file.
%% @equiv sync(Handle, sync)
-spec sync(Handle) -> ok | {error, term()} when
    Handle :: handle().
sync(Handle) ->
    sync(Handle, sync).

%% @doc Synchronize the memory mapping with the underlying file.
%%
%% `sync' mode waits for the operation to complete (MS_SYNC).
%% `async' mode returns immediately (MS_ASYNC).
-spec sync(Handle, Mode) -> ok | {error, term()} when
    Handle :: handle(),
    Mode :: sync_mode().
sync(Handle, Mode) when Mode =:= sync; Mode =:= async ->
    nif_sync(Handle, Mode);
sync(_, _) ->
    {error, badarg}.

%% @doc Truncate or extend the file to the specified size.
%%
%% Resizes the underlying file and remaps the memory region.
%% Existing data beyond `NewSize' is lost.
-spec truncate(Handle, NewSize) -> ok | {error, term()} when
    Handle :: handle(),
    NewSize :: non_neg_integer().
truncate(Handle, NewSize) when NewSize >= 0 ->
    nif_truncate(Handle, NewSize);
truncate(_, _) ->
    {error, badarg}.

%% @doc Provide advice about expected access patterns.
%%
%% Hints help the OS optimize memory management:
%% <ul>
%%   <li>`normal' - No special treatment</li>
%%   <li>`random' - Expect random access</li>
%%   <li>`sequential' - Expect sequential access</li>
%%   <li>`willneed' - Will need these pages soon</li>
%%   <li>`dontneed' - Won't need these pages soon</li>
%% </ul>
-spec advise(Handle, Offset, Length, Hint) -> ok | {error, term()} when
    Handle :: handle(),
    Offset :: non_neg_integer(),
    Length :: non_neg_integer(),
    Hint :: advise_hint().
advise(Handle, Offset, Length, Hint) when Offset >= 0, Length >= 0 ->
    nif_advise(Handle, Offset, Length, Hint);
advise(_, _, _, _) ->
    {error, badarg}.

%% @doc Get the current size of the memory-mapped region.
-spec position(Handle) -> {ok, non_neg_integer()} | {error, term()} when
    Handle :: handle().
position(Handle) ->
    nif_position(Handle).

%% NIF stubs
-spec nif_open(binary(), mode(), [open_option()]) -> {ok, handle()} | {error, term()}.
nif_open(_Path, _Mode, _Options) -> ?NIF_NOT_LOADED.

-spec nif_close(handle()) -> ok | {error, term()}.
nif_close(_Handle) -> ?NIF_NOT_LOADED.

-spec nif_pread(handle(), non_neg_integer(), non_neg_integer()) -> {ok, binary()} | {error, term()}.
nif_pread(_Handle, _Offset, _Length) -> ?NIF_NOT_LOADED.

-spec nif_pwrite(handle(), non_neg_integer(), binary()) -> ok | {error, term()}.
nif_pwrite(_Handle, _Offset, _Data) -> ?NIF_NOT_LOADED.

-spec nif_sync(handle(), sync_mode()) -> ok | {error, term()}.
nif_sync(_Handle, _Mode) -> ?NIF_NOT_LOADED.

-spec nif_truncate(handle(), non_neg_integer()) -> ok | {error, term()}.
nif_truncate(_Handle, _NewSize) -> ?NIF_NOT_LOADED.

-spec nif_advise(handle(), non_neg_integer(), non_neg_integer(), advise_hint()) -> ok | {error, term()}.
nif_advise(_Handle, _Offset, _Length, _Hint) -> ?NIF_NOT_LOADED.

-spec nif_position(handle()) -> {ok, non_neg_integer()} | {error, term()}.
nif_position(_Handle) -> ?NIF_NOT_LOADED.

-spec nif_region_binary(handle(), non_neg_integer(), non_neg_integer()) ->
        {ok, binary()} | {error, term()}.
nif_region_binary(_Handle, _Offset, _Length) -> ?NIF_NOT_LOADED.
