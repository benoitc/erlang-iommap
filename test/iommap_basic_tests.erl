%% @doc Basic tests for iommap module.
-module(iommap_basic_tests).

-include_lib("eunit/include/eunit.hrl").
-include_lib("kernel/include/file.hrl").

%% Test fixtures
setup() ->
    TestDir = "/tmp/iommap_test_" ++ integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    TestDir.

cleanup(TestDir) ->
    os:cmd("rm -rf " ++ TestDir),
    ok.

%% Generator
iommap_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(TestDir) ->
         [
          {"open and close new file", fun() -> open_close_new(TestDir) end},
          {"open existing file", fun() -> open_existing(TestDir) end},
          {"read and write operations", fun() -> read_write(TestDir) end},
          {"sync operations", fun() -> sync_ops(TestDir) end},
          {"position returns size", fun() -> position_test(TestDir) end},
          {"read-only mode", fun() -> read_only_mode(TestDir) end},
          {"private mapping", fun() -> private_mapping(TestDir) end}
         ]
     end}.

%% Test cases
open_close_new(TestDir) ->
    Path = filename:join(TestDir, "new_file.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),
    ?assert(is_reference(H)),
    ok = iommap:close(H),
    %% Verify file exists and has correct size
    {ok, Info} = file:read_file_info(Path),
    ?assertEqual(4096, Info#file_info.size).

open_existing(TestDir) ->
    Path = filename:join(TestDir, "existing_file.dat"),
    %% Create file with data
    Data = <<"Hello, world!">>,
    ok = file:write_file(Path, Data),
    %% Open with iommap
    {ok, H} = iommap:open(Path, read_write, []),
    %% Verify we can read the data
    {ok, ReadData} = iommap:pread(H, 0, byte_size(Data)),
    ?assertEqual(Data, ReadData),
    ok = iommap:close(H).

read_write(TestDir) ->
    Path = filename:join(TestDir, "rw_file.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 1024}]),

    %% Write some data
    WriteData = <<"Hello, iommap!">>,
    ok = iommap:pwrite(H, 0, WriteData),

    %% Read it back
    {ok, ReadData} = iommap:pread(H, 0, byte_size(WriteData)),
    ?assertEqual(WriteData, ReadData),

    %% Write at offset
    WriteData2 = <<"World">>,
    ok = iommap:pwrite(H, 100, WriteData2),
    {ok, ReadData2} = iommap:pread(H, 100, byte_size(WriteData2)),
    ?assertEqual(WriteData2, ReadData2),

    ok = iommap:close(H).

sync_ops(TestDir) ->
    Path = filename:join(TestDir, "sync_file.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 1024}]),

    ok = iommap:pwrite(H, 0, <<"test data">>),

    %% Sync mode
    ok = iommap:sync(H),
    ok = iommap:sync(H, sync),

    %% Async mode
    ok = iommap:sync(H, async),

    ok = iommap:close(H).

position_test(TestDir) ->
    Path = filename:join(TestDir, "position_file.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 8192}]),

    {ok, Size} = iommap:position(H),
    ?assertEqual(8192, Size),

    ok = iommap:close(H).

read_only_mode(TestDir) ->
    Path = filename:join(TestDir, "readonly_file.dat"),
    %% Create file first
    ok = file:write_file(Path, <<"readonly data">>),

    %% Open read-only
    {ok, H} = iommap:open(Path, read, []),

    %% Read should work
    {ok, Data} = iommap:pread(H, 0, 8),
    ?assertEqual(<<"readonly">>, Data),

    %% Write should fail
    ?assertEqual({error, eacces}, iommap:pwrite(H, 0, <<"test">>)),

    ok = iommap:close(H).

private_mapping(TestDir) ->
    Path = filename:join(TestDir, "private_file.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 1024}, private]),

    %% Write data (to private copy)
    ok = iommap:pwrite(H, 0, <<"private data">>),

    %% Read it back
    {ok, Data} = iommap:pread(H, 0, 12),
    ?assertEqual(<<"private data">>, Data),

    ok = iommap:close(H),

    %% File should still be zeros (private mapping doesn't write back)
    {ok, FileData} = file:read_file(Path),
    ?assertEqual(<<0:1024/unit:8>>, FileData).
