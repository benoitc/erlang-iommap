%% @doc Regression test for the FreeBSD segfault that fires when
%% iommap_nif.so is loaded into the same BEAM as another NIF and the
%% region_binary load pattern is exercised in a tight loop.
%%
%% This is the smallest reproducer of the crash that previously
%% manifested in erllama's eunit suite on FreeBSD 14.2 / 14.4. The
%% helper NIF (iommap_repro_helper) is the "second NIF": it does
%% nothing but coexist in the process and register its own resource
%% types.
%%
%% Success criterion: 200 iterations of {open, region_binary, close,
%% touch every byte, drop, gc} complete and eunit prints `N tests
%% passed`. A failure would be observed as the BEAM exiting before
%% eunit can finish reporting.
-module(iommap_two_nif_tests).

-include_lib("eunit/include/eunit.hrl").

iommap_two_nif_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(Ctx) ->
         [
          {"helper NIF loads alongside iommap",
           ?_assertEqual(ok, iommap_repro_helper:ensure_loaded())},
          {"region_binary load pattern in a tight loop",
           {timeout, 120, ?_test(run_cycles(Ctx, 200))}},
          {"region_binary cycle on a dirty-bound process",
           {timeout, 120, ?_test(run_cycles_offloaded(Ctx, 200))}}
         ]
     end}.

setup() ->
    %% Helper NIF is built by test/build_helper.sh via the eunit
    %% pre_hook. If the .so is missing, fail loudly here so the test
    %% report points at the build step rather than the test body.
    case iommap_repro_helper:ensure_loaded() of
        ok -> ok;
        Other ->
            erlang:error({helper_nif_not_loaded, Other})
    end,
    TestDir = "/tmp/iommap_two_nif_test_"
              ++ integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    Path = filename:join(TestDir, "data.bin"),
    Data = crypto:strong_rand_bytes(64 * 1024),
    ok = file:write_file(Path, Data),
    #{dir => TestDir, path => Path, data => Data,
      crc => erlang:crc32(Data)}.

cleanup(#{dir := Dir}) ->
    os:cmd("rm -rf " ++ Dir),
    ok.

%% Mirrors the erllama disk-tier load pattern that crashes on FreeBSD
%% when erllama_nif and iommap_nif are both loaded in the same BEAM:
%% open read-only, take a region_binary, close inside the same call,
%% touch every byte of the binary, drop the binary, GC. Repeat in a
%% tight loop. Allocate one of each helper resource per iteration so
%% the helper NIF is part of the per-iteration churn (its resource
%% destructors run alongside the iommap mapping/handle destructors).
run_cycles(#{path := Path, data := Data, crc := Expected}, Iters) ->
    lists:foreach(
        fun(_) ->
            {_HA, _HB} = iommap_repro_helper:make_pair(),
            {ok, H} = iommap:open(Path, read, []),
            {ok, B} =
                try
                    iommap:region_binary(H, 0, byte_size(Data))
                after
                    iommap:close(H)
                end,
            ?assertEqual(Expected, erlang:crc32(B)),
            _Forget = B,
            erlang:garbage_collect()
        end,
        lists:seq(1, Iters)).

%% Same loop, but running inside a freshly-spawned process per chunk so
%% the iommap NIF is exercised across many short-lived BEAM processes
%% (and therefore across many distinct scheduler-thread / process
%% combinations). This mirrors the way erllama's eunit suite hands off
%% loaded binaries from a worker process to the test process.
run_cycles_offloaded(Ctx, Iters) ->
    Parent = self(),
    Pid = spawn_link(fun() ->
        run_cycles(Ctx, Iters),
        Parent ! {self(), done}
    end),
    receive
        {Pid, done} -> ok
    after 60000 ->
        erlang:exit(Pid, kill),
        erlang:error(offloaded_cycles_timeout)
    end.
