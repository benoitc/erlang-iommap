%% @doc Lifecycle / signal-handler smoke tests for iommap.
%%
%% Test A: in-protected SIGBUS still caught after the categorize-and-don't-chain
%% rewrite of the SIGBUS handler. Regression guard.
%%
%% Test B: a basic round-trip survives an extra code:load_file/1 (exercises the
%% on_upgrade path and confirms the install flag does the right thing on a
%% second on_load call).
%%
%% Test B is a wiring smoke. It does NOT prove that on_unload runs in EUnit;
%% code:purge/1 inside the same VM may be blocked, and NIF DSO unload semantics
%% differ across OTP versions. Use the manual escript recipe in
%% guides/features.md if you need to exercise the unload restoration path.
-module(iommap_lifecycle_tests).

-include_lib("eunit/include/eunit.hrl").

setup() ->
    TestDir = "/tmp/iommap_lifecycle_" ++
              integer_to_list(erlang:unique_integer([positive])),
    ok = file:make_dir(TestDir),
    TestDir.

cleanup(TestDir) ->
    os:cmd("rm -rf " ++ TestDir),
    ok.

iommap_lifecycle_test_() ->
    {setup,
     fun setup/0,
     fun cleanup/1,
     fun(TestDir) ->
         %% Test A: truncate-then-touch SIGBUS triggering varies by OS/FS; gate
         %% the test to platforms where it is reliable. macOS and Linux are
         %% known-good. FreeBSD/OpenBSD: not asserted today; if the existing
         %% iommap_two_nif_tests starts working there, broaden this gate.
         SigbusReliable = case os:type() of
                              {unix, darwin}  -> true;
                              {unix, linux}   -> true;
                              _               -> false
                          end,
         lists:flatten(
           [
            [{"sigbus still caught after install rewrite",
              fun() -> sigbus_in_protected(TestDir) end} || SigbusReliable],
            [{"round-trip after code:load_file/1",
              fun() -> roundtrip_after_reload(TestDir) end}],
            [{"sigbus still caught after upgrade + purge",
              fun() -> sigbus_after_upgrade_purge(TestDir) end} || SigbusReliable]
           ])
     end}.

%% Test A: open mmap, truncate file from outside, pread past new EOF.
%% Expect {error, sigbus} — proves the protected-region path still catches.
sigbus_in_protected(TestDir) ->
    Path = filename:join(TestDir, "sigbus.dat"),
    %% Create a 4 KiB mapping.
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),
    ok = iommap:pwrite(H, 0, <<"data">>),
    %% Truncate the underlying file to zero from outside the NIF.
    %% The mapping still covers 4 KiB but the file backing is gone.
    {ok, Fd} = file:open(Path, [write]),
    ok = file:truncate(Fd),
    ok = file:close(Fd),
    %% Reading the now-unbacked region should SIGBUS inside the protected
    %% memcpy and come back as {error, sigbus} rather than crashing the VM.
    Result = iommap:pread(H, 0, 4096),
    ?assertEqual({error, sigbus}, Result),
    %% Close should succeed regardless.
    ok = iommap:close(H).

%% Test B: call code:load_file(iommap) again to exercise on_upgrade, then
%% confirm a basic round-trip still works.
roundtrip_after_reload(TestDir) ->
    %% Force a reload of the iommap module. This re-runs ERL_NIF_INIT on the
    %% upgrade path; with the install flag in place, the second sigaction
    %% must be a no-op rather than overwriting prior_disposition with our
    %% own handler.
    {module, iommap} = code:load_file(iommap),
    Path = filename:join(TestDir, "reload.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 1024}]),
    ok = iommap:pwrite(H, 0, <<"after-reload">>),
    {ok, <<"after-reload">>} = iommap:pread(H, 0, byte_size(<<"after-reload">>)),
    ok = iommap:close(H).

%% Test C: code:load_file/1 followed by code:purge/1 runs the old module's
%% on_unload while the new module is live. dlopen of the same .so path
%% returns the same image, so both share the NIF's statics; the unload
%% must not tear down the SIGBUS handler or the per-thread state the
%% live module still uses. Before the load refcount this crashed the VM
%% on the next SIGBUS and made pread/pwrite return {error, enomem}.
sigbus_after_upgrade_purge(TestDir) ->
    %% Test B may have left old code behind; purge it first.
    _ = code:purge(iommap),
    {module, iommap} = code:load_file(iommap),
    _ = code:purge(iommap),
    Path = filename:join(TestDir, "sigbus_after_purge.dat"),
    {ok, H} = iommap:open(Path, read_write, [create, {size, 4096}]),
    ok = iommap:pwrite(H, 0, <<"data">>),
    {ok, Fd} = file:open(Path, [write]),
    ok = file:truncate(Fd),
    ok = file:close(Fd),
    ?assertEqual({error, sigbus}, iommap:pread(H, 0, 4096)),
    ok = iommap:close(H).
