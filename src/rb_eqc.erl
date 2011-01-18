-module(rb_eqc).
-compile(export_all).

-include_lib("eqc/include/eqc.hrl").
-include_lib("eqc/include/eqc_statem.hrl").

-record(state, {
          step,
          tree,
          map
         }).

setup() ->
    catch eqc_c:stop(),
    eqc_c:start(rbt, [{c_src, "c_src/red_black_tree.c"}, {additional_files, ["c_src/eqcfoo.c", "c_src/stack.c", "c_src/misc.c"]}]).    

prop_rb() ->
    ?FORALL(Cmds, commands(?MODULE),
            collect({len_div_100, length(Cmds) div 100},
            aggregate(eqc_statem:command_names(Cmds),
            begin
                {H, S, Res} = run_commands(?MODULE, Cmds),
                ?WHENFAIL(
                   io:format("Time: ~p\nHistory length: ~p\nHistory: ~p\nState: ~p\nRes: ~p\n",[time(), length(H), H,S,Res]),
                   Res == ok)
            end))).

initial_state() ->
    #state{step = 1,
           map = orddict:new()}.

command(S) when S#state.step == 1 ->
    {call, ?MODULE, new_tree, []};
command(S) ->
    elements([
              {call, ?MODULE, insert, [{var,1}, gen_key(), gen_val()]},
              {call, ?MODULE, get_exact, [{var,1}, gen_key()]},
              {call, ?MODULE, delete, [{var,1}, gen_likely_key(S)]},
              {call, ?MODULE, get_first, [{var,1}]},
              {call, ?MODULE, get_last, [{var,1}]},
              {call, ?MODULE, get_next, [{var,1}, gen_likely_key(S)]}
             ]).

gen_likely_key(S) ->
    ?LET(OtherKey, gen_key(),
         elements([K || {K, _V} <- S#state.map] ++ [OtherKey])).

gen_key() ->
    elements(["k1","k2","k3","k4","k5","k6","k7","k8","k9","k10","k11",
              "k12","k1a","k1m","k1z","k1aaa","k1mmm","k1zzz","k19","k20"]).

gen_val() ->
    int().

precondition(_S, {call, _, _, _}) ->
    true.

postcondition(_S, {call, _, new_tree, _}, _Res) ->
    true;
postcondition(_S, {call, _, insert, _}, Res) ->
    0 = rbt:test_node_is_null(Res),
    true;
postcondition(S, {call, _, get_exact, [_Tree, Key]}, Res) ->
    case lists:keysearch(Key, 1, S#state.map) of
        {value, {_, Val}} ->
            %X% io:format("DBG: ~s line ~p Res ~p\n", [?MODULE, ?LINE, Res]),
            0 = rbt:test_node_is_null(Res),
            {test_t, _Ptr, TreeVal} = rbt:test_node_to_test_t(Res),
            %X% io:format("DBG: ~s ~p\n", [?MODULE, ?LINE]),
            TreeVal == Val;
        false ->
            %X% io:format("DBG: Res ~p\n", [Res]),
            1 == rbt:test_node_is_null(Res)
    end;
postcondition(S, {call, _, delete, [_Tree, Key]}, Res) ->
    case lists:keysearch(Key, 1, S#state.map) of
        {value, {_, _Val}} ->
            Res == 1;
        false ->
            Res == 0
    end;
postcondition(S, {call, _, get_first, [_Tree]}, Res) ->
    postcondition_get_firstlast(S, Res, fun(L) -> L end);
postcondition(S, {call, _, get_last, [_Tree]}, Res) ->
    postcondition_get_firstlast(S, Res, fun(L) -> lists:reverse(L) end);

postcondition(S, {call, _, get_next, [_Tree, PrevKey]}, Res) ->
    Remaining = lists:dropwhile(fun({K, _V}) when K =< PrevKey -> true;
                                   (_)                         -> false
                                end, lists:sort(S#state.map)),
    case rbt:test_node_is_null(Res) of
        1 ->
            Remaining == [];
        0 ->
            %X% io:format("pre: Res = ~p\n", [Res]),
            {test_t, Ptr, TreeVal} = rbt:test_node_to_test_t(Res),
            %X% io:format("pre: Res match = ~p\n", [{test_t, Ptr, TreeVal}]),
            TreeKey = eqc_c:read_string(Ptr),
            % io:format("TreeKey = ~s\n", [TreeKey]),
            % io:format("sorted map = ~p\n", [lists:sort(S#state.map)]),
            % io:format("Remaining = ~p\n", [Remaining]),
            if Remaining == [] ->
                    false_remaining_empty;
               true ->
                    {TreeKey, TreeVal} == hd(Remaining)
            end
    end.

postcondition_get_firstlast(S, Res, ListModFun) ->
    case rbt:test_node_is_null(Res) of
        1 ->
            S#state.map == [];
        0 ->
            {test_t, Ptr, TreeVal} = rbt:test_node_to_test_t(Res),
            TreeKey = eqc_c:read_string(Ptr),
            if S#state.map == [] ->
                    false_map_empty;
               true ->
                    {TreeKey, TreeVal} == hd(ListModFun(lists:sort(S#state.map)))
            end
    end.

next_state(S, V, R) ->
    S2 = next_state2(S, V, R),
    S2#state{step = S2#state.step + 1}.

next_state2(S, _V, {call, _, new_tree, _}) ->
    S;
next_state2(#state{map = OldMap} = S, _V, {call, _, insert, [_Tree, Key, Val]}) ->
    S#state{map = [{Key, Val}|lists:keydelete(Key, 1, OldMap)]};
next_state2(#state{map = OldMap} = S, _V, {call, _, delete, [_Tree, Key]}) ->
    S#state{map = lists:keydelete(Key, 1, OldMap)};
next_state2(S, _V, {call, _, get_exact, _}) ->
    S;
next_state2(S, _V, {call, _, get_first, _}) ->
    S;
next_state2(S, _V, {call, _, get_last, _}) ->
    S;
next_state2(S, _V, {call, _, get_next, _}) ->
    S.

%%%%%%%%%%%%%%%%%%%

new_tree() ->
    rbt:test_new_tree().

insert(Tree, Key, Val) ->
    Res = rbt:test_insert(Tree, Key, Val),
    0 = rbt:test_node_is_null(Res),
    Res.

get_exact(Tree, Key) ->
    rbt:test_exact_query(Tree, Key).

delete(Tree, Key) ->
    rbt:test_delete(Tree, Key).

get_first(Tree) ->
    rbt:test_get_first(Tree).

get_last(Tree) ->
    rbt:test_get_last(Tree).

get_next(Tree, Key) ->
    rbt:test_get_next(Tree, Key).
