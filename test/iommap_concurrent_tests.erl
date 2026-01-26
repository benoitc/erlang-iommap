%% @doc Concurrent access tests for iommap module.
-module(iommap_concurrent_tests).

-include_lib("eunit/include/eunit.hrl").

%% Test fixtures
setup() ->
    TestDir = "/tmp/iommap_concurrent_test_" ++ integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    TestDir.

cleanup(TestDir) ->
    os:cmd("rm -rf " ++ TestDir),
    ok.

%% Generator
iommap_concurrent_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(TestDir) ->
         [
          {"concurrent reads", ?_test(concurrent_reads(TestDir))},
          {"concurrent writes different regions", ?_test(concurrent_writes_regions(TestDir))},
          {"reader writer mixed", ?_test(reader_writer_mixed(TestDir))}
         ]
     end}.

%% Test cases
concurrent_reads(TestDir) ->
    Path = filename:join(TestDir, "concurrent_read.dat"),
    Data = crypto:strong_rand_bytes(4096),
    ok = file:write_file(Path, Data),

    {ok, H} = iommap:open(Path, read, []),

    %% Spawn multiple readers
    Parent = self(),
    NumReaders = 10,
    Pids = [spawn_link(fun() ->
        Results = [begin
            {ok, Read} = iommap:pread(H, 0, 4096),
            Read =:= Data
        end || _ <- lists:seq(1, 100)],
        Parent ! {self(), lists:all(fun(X) -> X end, Results)}
    end) || _ <- lists:seq(1, NumReaders)],

    %% Wait for all readers
    Results = [receive {Pid, Result} -> Result end || Pid <- Pids],
    ?assert(lists:all(fun(X) -> X end, Results)),

    ok = iommap:close(H).

concurrent_writes_regions(TestDir) ->
    Path = filename:join(TestDir, "concurrent_write_regions.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),

    %% Spawn writers for different regions
    Parent = self(),
    Regions = [{0, 1000}, {1000, 1000}, {2000, 1000}, {3000, 1000}],

    Pids = [spawn_link(fun() ->
        Data = list_to_binary(lists:duplicate(Size, (Offset div 1000) + $A)),
        ok = iommap:pwrite(H, Offset, Data),
        Parent ! {self(), ok}
    end) || {Offset, Size} <- Regions],

    %% Wait for all writers
    [receive {Pid, ok} -> ok end || Pid <- Pids],

    %% Verify each region
    {ok, Region0} = iommap:pread(H, 0, 1000),
    {ok, Region1} = iommap:pread(H, 1000, 1000),
    {ok, Region2} = iommap:pread(H, 2000, 1000),
    {ok, Region3} = iommap:pread(H, 3000, 1000),

    ?assertEqual(list_to_binary(lists:duplicate(1000, $A)), Region0),
    ?assertEqual(list_to_binary(lists:duplicate(1000, $B)), Region1),
    ?assertEqual(list_to_binary(lists:duplicate(1000, $C)), Region2),
    ?assertEqual(list_to_binary(lists:duplicate(1000, $D)), Region3),

    ok = iommap:close(H).

reader_writer_mixed(TestDir) ->
    Path = filename:join(TestDir, "mixed_rw.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),

    %% Initialize with known data
    InitData = list_to_binary(lists:duplicate(4096, $X)),
    ok = iommap:pwrite(H, 0, InitData),

    Parent = self(),

    %% Writer process - writes incrementing values
    WriterPid = spawn_link(fun() ->
        writer_loop(H, 0, 100),
        Parent ! {self(), done}
    end),

    %% Reader process - reads and checks consistency
    ReaderPid = spawn_link(fun() ->
        reader_loop(H, 100),
        Parent ! {self(), done}
    end),

    %% Wait for both
    receive {WriterPid, done} -> ok end,
    receive {ReaderPid, done} -> ok end,

    ok = iommap:close(H).

writer_loop(_H, N, N) ->
    ok;
writer_loop(H, I, N) ->
    Data = <<I:8>>,
    Offset = (I rem 4096),
    ok = iommap:pwrite(H, Offset, Data),
    timer:sleep(1),
    writer_loop(H, I + 1, N).

reader_loop(_H, 0) ->
    ok;
reader_loop(H, N) ->
    %% Just verify we can read without errors
    case iommap:pread(H, 0, 100) of
        {ok, Data} when is_binary(Data) ->
            ok;
        {error, Reason} ->
            error({read_failed, Reason})
    end,
    timer:sleep(1),
    reader_loop(H, N - 1).
