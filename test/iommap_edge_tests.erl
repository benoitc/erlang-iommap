%% @doc Edge case tests for iommap module.
-module(iommap_edge_tests).

-include_lib("eunit/include/eunit.hrl").
-include_lib("kernel/include/file.hrl").

%% Test fixtures
setup() ->
    TestDir = "/tmp/iommap_edge_test_" ++ integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    TestDir.

cleanup(TestDir) ->
    os:cmd("rm -rf " ++ TestDir),
    ok.

%% Generator
iommap_edge_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(TestDir) ->
         [
          {"out of bounds read", fun() -> out_of_bounds_read(TestDir) end},
          {"out of bounds write", fun() -> out_of_bounds_write(TestDir) end},
          {"double close", fun() -> double_close(TestDir) end},
          {"operations on closed handle", fun() -> closed_handle(TestDir) end},
          {"zero length read", fun() -> zero_length_read(TestDir) end},
          {"truncate extend", fun() -> truncate_extend(TestDir) end},
          {"truncate shrink", fun() -> truncate_shrink(TestDir) end},
          {"advise operations", fun() -> advise_ops(TestDir) end},
          {"file not found", fun() -> file_not_found(TestDir) end},
          {"invalid arguments", fun() -> invalid_args(TestDir) end},
          {"read mode rejects create/truncate", fun() -> read_mode_options(TestDir) end},
          {"size option never shrinks", fun() -> size_never_shrinks(TestDir) end},
          {"embedded nul in path", fun() -> nul_in_path(TestDir) end},
          {"unicode path", fun() -> unicode_path(TestDir) end}
         ]
     end}.

%% Test cases
out_of_bounds_read(TestDir) ->
    Path = filename:join(TestDir, "bounds_read.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    %% Try to read beyond file size
    ?assertEqual({error, out_of_bounds}, iommap:pread(H, 90, 20)),
    ?assertEqual({error, out_of_bounds}, iommap:pread(H, 100, 1)),

    ok = iommap:close(H).

out_of_bounds_write(TestDir) ->
    Path = filename:join(TestDir, "bounds_write.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    %% Try to write beyond file size
    ?assertEqual({error, out_of_bounds}, iommap:pwrite(H, 90, <<"12345678901234567890">>)),
    ?assertEqual({error, out_of_bounds}, iommap:pwrite(H, 100, <<"x">>)),

    ok = iommap:close(H).

double_close(TestDir) ->
    Path = filename:join(TestDir, "double_close.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    ok = iommap:close(H),
    %% Second close should return error
    ?assertEqual({error, closed}, iommap:close(H)).

closed_handle(TestDir) ->
    Path = filename:join(TestDir, "closed_handle.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),
    ok = iommap:close(H),

    %% All operations should fail
    ?assertEqual({error, closed}, iommap:pread(H, 0, 10)),
    ?assertEqual({error, closed}, iommap:pwrite(H, 0, <<"test">>)),
    ?assertEqual({error, closed}, iommap:sync(H)),
    ?assertEqual({error, closed}, iommap:position(H)),
    ?assertEqual({error, closed}, iommap:truncate(H, 200)),
    ?assertEqual({error, closed}, iommap:advise(H, 0, 100, normal)).

zero_length_read(TestDir) ->
    Path = filename:join(TestDir, "zero_read.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    %% Zero length read should succeed and return empty binary
    {ok, Data} = iommap:pread(H, 0, 0),
    ?assertEqual(<<>>, Data),

    ok = iommap:close(H).

truncate_extend(TestDir) ->
    Path = filename:join(TestDir, "truncate_extend.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    %% Write some data
    ok = iommap:pwrite(H, 0, <<"original">>),

    %% Extend the file
    ok = iommap:truncate(H, 500),
    {ok, NewSize} = iommap:position(H),
    ?assertEqual(500, NewSize),

    %% Original data should still be there
    {ok, Data} = iommap:pread(H, 0, 8),
    ?assertEqual(<<"original">>, Data),

    %% Can now write to extended region
    ok = iommap:pwrite(H, 200, <<"extended">>),
    {ok, ExtData} = iommap:pread(H, 200, 8),
    ?assertEqual(<<"extended">>, ExtData),

    ok = iommap:close(H).

truncate_shrink(TestDir) ->
    Path = filename:join(TestDir, "truncate_shrink.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 1000}]),

    %% Write data at the end
    ok = iommap:pwrite(H, 900, <<"at end">>),

    %% Shrink the file
    ok = iommap:truncate(H, 500),
    {ok, NewSize} = iommap:position(H),
    ?assertEqual(500, NewSize),

    %% Data beyond new size should be gone
    ?assertEqual({error, out_of_bounds}, iommap:pread(H, 900, 6)),

    ok = iommap:close(H).

advise_ops(TestDir) ->
    Path = filename:join(TestDir, "advise.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),

    %% Test all advise hints
    ok = iommap:advise(H, 0, 4096, normal),
    ok = iommap:advise(H, 0, 4096, random),
    ok = iommap:advise(H, 0, 4096, sequential),
    ok = iommap:advise(H, 0, 4096, willneed),
    ok = iommap:advise(H, 0, 4096, dontneed),

    %% Out of bounds advise
    ?assertEqual({error, out_of_bounds}, iommap:advise(H, 4000, 200, normal)),

    ok = iommap:close(H).

file_not_found(TestDir) ->
    Path = filename:join([TestDir, "nonexistent", "file.dat"]),
    ?assertMatch({error, _}, iommap:open(Path, read_write, [])).

invalid_args(TestDir) ->
    Path = filename:join(TestDir, "invalid.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 100}]),

    %% Negative offsets should be rejected at Erlang level. The value
    %% is built at runtime so dialyzer does not flag the spec violation.
    Neg = list_to_integer("-1"),
    ?assertEqual({error, badarg}, iommap:pread(H, Neg, 10)),
    ?assertEqual({error, badarg}, iommap:pwrite(H, Neg, <<"test">>)),

    ok = iommap:close(H).

read_mode_options(TestDir) ->
    Path = filename:join(TestDir, "ro_opts.dat"),
    ok = file:write_file(Path, <<"0123456789">>),

    %% O_RDONLY|O_TRUNC truncates the file on Linux; make sure we
    %% refuse before touching the file.
    ?assertEqual({error, einval}, iommap:open(Path, read, [truncate])),
    ?assertEqual({ok, <<"0123456789">>}, file:read_file(Path)),

    %% O_RDONLY|O_CREAT would leave an empty file behind.
    Missing = filename:join(TestDir, "ro_missing.dat"),
    ?assertEqual({error, einval}, iommap:open(Missing, read, [create, {size, 10}])),
    ?assertEqual({error, enoent}, file:read_file(Missing)),

    {ok, H} = iommap:open(Path, read, []),
    ?assertEqual({ok, <<"0123456789">>}, iommap:pread(H, 0, 10)),
    ok = iommap:close(H).

size_never_shrinks(TestDir) ->
    Path = filename:join(TestDir, "no_shrink.dat"),
    ok = file:write_file(Path, binary:copy(<<"x">>, 1000)),

    %% Existing file larger than {size, N} with create: mapping covers
    %% the whole file and the file is not truncated on any platform.
    {ok, H1} = iommap:open(Path, read_write, [create, {size, 100}]),
    ?assertEqual({ok, 1000}, iommap:position(H1)),
    ok = iommap:close(H1),
    {ok, #file_info{size = 1000}} = file:read_file_info(Path),

    %% Existing file smaller than {size, N} is grown.
    {ok, H2} = iommap:open(Path, read_write, [create, {size, 2000}]),
    ?assertEqual({ok, 2000}, iommap:position(H2)),
    ok = iommap:close(H2),

    %% Explicit truncate then size gives exactly N.
    {ok, H3} = iommap:open(Path, read_write, [truncate, {size, 50}]),
    ?assertEqual({ok, 50}, iommap:position(H3)),
    ok = iommap:close(H3).

nul_in_path(TestDir) ->
    Good = filename:join(TestDir, "nul.dat"),
    ok = file:write_file(Good, <<"data">>),
    Bad = iolist_to_binary([Good, 0, "ignored"]),
    ?assertError(badarg, iommap:open(Bad, read, [])).

unicode_path(TestDir) ->
    Path = filename:join(TestDir, [16#e9, 16#4e2d, "_unicode.dat"]),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 8}]),
    ok = iommap:pwrite(H, 0, <<"unicode!">>),
    ok = iommap:close(H),
    ?assertEqual({ok, <<"unicode!">>}, file:read_file(Path)).
