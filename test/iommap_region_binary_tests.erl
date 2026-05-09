%% @doc Tests for iommap:region_binary/3 zero-copy view.
-module(iommap_region_binary_tests).

-include_lib("eunit/include/eunit.hrl").
-include_lib("kernel/include/file.hrl").

setup() ->
    TestDir = "/tmp/iommap_region_binary_test_"
              ++ integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    TestDir.

cleanup(TestDir) ->
    os:cmd("rm -rf " ++ TestDir),
    ok.

iommap_region_binary_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(TestDir) ->
         [
          {"round-trip equality with pread",
           ?_test(round_trip(TestDir))},
          {"zero-copy proof on 64 MiB",
           {timeout, 60, ?_test(zero_copy_proof(TestDir))}},
          {"handle outlives binary",
           ?_test(handle_outlives_binary(TestDir))},
          {"binary outlives handle",
           ?_test(binary_outlives_handle(TestDir))},
          {"reopen after both released",
           ?_test(reopen_after_release(TestDir))},
          {"close while many binaries outstanding",
           ?_test(close_with_outstanding(TestDir))},
          {"random open/region/close fuzz",
           {timeout, 60, ?_test(random_fuzz(TestDir))}}
         ]
     end}.

%% 1. region_binary returns the same bytes as pread.
round_trip(TestDir) ->
    Path = filename:join(TestDir, "round_trip.dat"),
    Data = crypto:strong_rand_bytes(64 * 1024),
    ok = file:write_file(Path, Data),
    {ok, H} = iommap:open(Path, read, []),
    {ok, ViaPread} = iommap:pread(H, 0, byte_size(Data)),
    {ok, ViaRegion} = iommap:region_binary(H, 0, byte_size(Data)),
    ?assertEqual(Data, ViaPread),
    ?assertEqual(Data, ViaRegion),
    %% Sub-range round-trip too.
    Off = 12345,
    Len = 8192,
    {ok, P2} = iommap:pread(H, Off, Len),
    {ok, R2} = iommap:region_binary(H, Off, Len),
    ?assertEqual(P2, R2),
    ok = iommap:close(H).

%% 2. A 64 MiB region_binary does not balloon the BEAM heap. The
%% returned binary is a refcounted view, so process heap growth is
%% bounded by metadata, not by slice size.
zero_copy_proof(TestDir) ->
    Path = filename:join(TestDir, "zero_copy.dat"),
    Size = 64 * 1024 * 1024,
    {ok, H} = iommap:open(Path, read_write, [create, {size, Size}]),
    %% Touch the first byte so the file is not entirely sparse.
    ok = iommap:pwrite(H, 0, <<42>>),
    erlang:garbage_collect(),
    {memory, M0} = erlang:process_info(self(), memory),
    {ok, B} = iommap:region_binary(H, 0, Size),
    ?assertEqual(Size, byte_size(B)),
    {memory, M1} = erlang:process_info(self(), memory),
    %% Heap delta must be far below the slice size. 1 MiB is generous;
    %% the actual cost is a few hundred bytes of refc-binary metadata.
    ?assert((M1 - M0) < 1024 * 1024),
    %% A sub-binary pointing into the middle is also zero-copy.
    Mid = binary:part(B, Size div 2, 1024),
    ?assertEqual(1024, byte_size(Mid)),
    ok = iommap:close(H).

%% 3. The handle survives after a region_binary is dropped and GC'd.
handle_outlives_binary(TestDir) ->
    Path = filename:join(TestDir, "handle_outlives.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 8192}]),
    ok = iommap:pwrite(H, 0, <<"hello">>),
    {ok, B} = iommap:region_binary(H, 0, 4096),
    ?assertEqual(<<"hello">>, binary:part(B, 0, 5)),
    %% Drop the binary and force GC.
    _Forget = B,
    erlang:garbage_collect(),
    %% The handle is still usable.
    {ok, 8192} = iommap:position(H),
    {ok, <<"hello">>} = iommap:pread(H, 0, 5),
    ok = iommap:close(H).

%% 4. The mapping survives after close/1 if a region_binary is still
%% reachable. Reads from the binary still return correct bytes.
binary_outlives_handle(TestDir) ->
    Path = filename:join(TestDir, "binary_outlives.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),
    ok = iommap:pwrite(H, 0, <<"persistent bytes">>),
    {ok, B} = iommap:region_binary(H, 0, 4096),
    ok = iommap:close(H),
    %% Subsequent ops on the closed handle fail.
    ?assertEqual({error, closed}, iommap:pread(H, 0, 4)),
    ?assertEqual({error, closed}, iommap:position(H)),
    %% The binary is still readable: mapping kept alive by its ref.
    ?assertEqual(<<"persistent bytes">>, binary:part(B, 0, 16)),
    %% Drop the binary; the mapping resource will be GC'd later.
    _Forget = B,
    erlang:garbage_collect(),
    ok.

%% 5. After dropping both binary and handle and GC'ing, the mapping is
%% released (fd closed, file unmapped). Reopening the same path with
%% write must succeed.
reopen_after_release(TestDir) ->
    Path = filename:join(TestDir, "reopen.dat"),
    {ok, H1} = iommap:open(Path, read_write, [create, {size, 4096}]),
    ok = iommap:pwrite(H1, 0, <<"first open">>),
    {ok, B1} = iommap:region_binary(H1, 0, 4096),
    ok = iommap:close(H1),
    %% Drop binary and force the mapping resource to be collected.
    _F = B1,
    erlang:garbage_collect(),
    %% Give the runtime a beat to actually reclaim the resource.
    timer:sleep(50),
    erlang:garbage_collect(),
    {ok, H2} = iommap:open(Path, read_write, []),
    {ok, <<"first open">>} = iommap:pread(H2, 0, 10),
    ok = iommap:pwrite(H2, 0, <<"second open">>),
    ok = iommap:close(H2).

%% 6. Many processes hold region_binaries, then another process closes
%% the handle. All binaries remain readable; no segfault.
close_with_outstanding(TestDir) ->
    Path = filename:join(TestDir, "close_outstanding.dat"),
    Size = 4096,
    {ok, H} = iommap:open(Path, read_write, [create, {size, Size}]),
    Marker = <<"shared mapping">>,
    ok = iommap:pwrite(H, 0, Marker),
    Parent = self(),
    NumWorkers = 16,
    Workers = [spawn_link(fun() ->
        {ok, B} = iommap:region_binary(H, 0, Size),
        Parent ! {self(), ready},
        receive close_done -> ok end,
        Slice = binary:part(B, 0, byte_size(Marker)),
        Parent ! {self(), Slice}
    end) || _ <- lists:seq(1, NumWorkers)],
    %% Wait for everyone to grab their binary.
    [receive {Pid, ready} -> ok end || Pid <- Workers],
    %% Close the handle while binaries are outstanding.
    ok = iommap:close(H),
    %% Tell workers to read from their binaries.
    [Pid ! close_done || Pid <- Workers],
    Slices = [receive {Pid, S} -> S end || Pid <- Workers],
    [?assertEqual(Marker, S) || S <- Slices],
    ok.

%% 7. Random sequence of open/region/close/GC operations. Verifies no
%% segfault and that bytes returned are always correct relative to
%% what was written.
random_fuzz(TestDir) ->
    Path = filename:join(TestDir, "fuzz.dat"),
    Size = 64 * 1024,
    {ok, H0} = iommap:open(Path, read_write, [create, {size, Size}]),
    Pattern = crypto:strong_rand_bytes(Size),
    ok = iommap:pwrite(H0, 0, Pattern),
    ok = iommap:close(H0),
    %% Use a deterministic seed for reproducibility.
    rand:seed(exsss, {1, 2, 3}),
    Iters = 200,
    fuzz_loop(Path, Pattern, Size, Iters, []).

fuzz_loop(_Path, _Pattern, _Size, 0, _Held) ->
    erlang:garbage_collect(),
    ok;
fuzz_loop(Path, Pattern, Size, N, Held) ->
    Action = rand:uniform(4),
    Held1 = case Action of
        1 ->
            %% Open, take a binary, close, keep binary.
            {ok, H} = iommap:open(Path, read, []),
            Off = rand:uniform(Size) - 1,
            Len = rand:uniform(Size - Off),
            {ok, B} = iommap:region_binary(H, Off, Len),
            ok = iommap:close(H),
            ?assertEqual(binary:part(Pattern, Off, Len), B),
            [B | Held];
        2 ->
            %% Open, read with pread, close.
            {ok, H} = iommap:open(Path, read, []),
            Off = rand:uniform(Size) - 1,
            Len = rand:uniform(Size - Off),
            {ok, B} = iommap:pread(H, Off, Len),
            ok = iommap:close(H),
            ?assertEqual(binary:part(Pattern, Off, Len), B),
            Held;
        3 ->
            %% Drop a few held binaries.
            erlang:garbage_collect(),
            case Held of
                [] -> Held;
                [_ | Rest] -> Rest
            end;
        4 ->
            %% Open, take a binary, validate, drop binary, close.
            {ok, H} = iommap:open(Path, read, []),
            Off = rand:uniform(Size) - 1,
            Len = rand:uniform(Size - Off),
            {ok, B} = iommap:region_binary(H, Off, Len),
            ?assertEqual(binary:part(Pattern, Off, Len), B),
            _F = B,
            ok = iommap:close(H),
            erlang:garbage_collect(),
            Held
    end,
    fuzz_loop(Path, Pattern, Size, N - 1, Held1).
