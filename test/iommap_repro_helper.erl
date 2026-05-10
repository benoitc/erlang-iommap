%% @doc Test-only helper NIF that coexists with iommap_nif inside the
%% same BEAM. See test/c_src/iommap_repro_helper.c for the rationale.
-module(iommap_repro_helper).

-export([ensure_loaded/0, make_pair/0]).

-on_load(init/0).

-define(NIF_NOT_LOADED, erlang:nif_error(nif_not_loaded)).

%% Best-effort NIF discovery: the helper .so is built into the iommap
%% application's priv directory by test/build_helper.sh.
init() ->
    PrivDir = case code:priv_dir(iommap) of
        {error, bad_name} ->
            AppFile = filename:dirname(code:which(?MODULE)),
            filename:join(filename:dirname(AppFile), "priv");
        Dir ->
            Dir
    end,
    SoName = filename:join(PrivDir, "iommap_repro_helper"),
    erlang:load_nif(SoName, 0).

ensure_loaded() ->
    case erlang:function_exported(?MODULE, make_pair, 0) of
        true -> ok;
        false -> {error, nif_not_loaded}
    end.

make_pair() -> ?NIF_NOT_LOADED.
