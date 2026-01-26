%%% @doc Memory-mapped file I/O for Erlang.
%%%
%%% This module provides cross-platform memory-mapped file access
%%% compatible with Linux, macOS, and BSD systems.
%%%
%%% == Example ==
%%% ```
%%% {ok, H} = iommap:open("/tmp/test.dat", read_write, [create, {size, 4096}]),
%%% ok = iommap:pwrite(H, 0, <<"Hello, iommap!">>),
%%% {ok, <<"Hello, iommap!">>} = iommap:pread(H, 0, 14),
%%% ok = iommap:sync(H),
%%% ok = iommap:close(H).
%%% '''
-module(iommap).

-export([open/2, open/3, close/1]).
-export([pread/3, pwrite/3]).
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
open(Path, Mode, Options) when is_list(Path); is_binary(Path) ->
    PathBin = iolist_to_binary(Path),
    nif_open(PathBin, Mode, Options);
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
