/**
 * Create a table for ev.Timer that gives access to the constructor for
 * timer objects.
 *
 * [-0, +1, ?]
 */
static int luaopen_ev_timer(lua_State *L) {
    lua_pop(L, create_timer_mt(L));

    lua_createtable(L, 0, 1);

    lua_pushcfunction(L, timer_new);
    lua_setfield(L, -2, "new");

    return 1;
}

/**
 * Create the timer metatable in the registry.
 *
 * [-0, +1, ?]
 */
static int create_timer_mt(lua_State *L) {

    static luaL_Reg fns[] = {
        { "again",         timer_again },
        { "stop",          timer_stop },
        { "start",         timer_start },
        { "clear_pending", timer_clear_pending },
        { "__gc",          timer_gc },
        { NULL, NULL }
    };
    luaL_newmetatable(L, TIMER_MT);
    add_watcher_mt(L);
    luaL_setfuncs(L, fns, 0);

    return 1;
}

/**
 * Create a new timer object.  Arguments:
 *   1 - callback function.
 *   2 - after (number of seconds until timer should trigger).
 *   3 - repeat (number of seconds to wait for consecutive timeouts).
 *
 * @see watcher_new()
 *
 * [+1, -0, ?]
 */
static int timer_new(lua_State* L) {
    ev_tstamp after  = luaL_checknumber(L, 2);
    ev_tstamp repeat = luaL_optnumber(L, 3, 0);
    ev_timer* timer;

    if ( repeat < 0.0 )
        luaL_argerror(L, 3, "repeat must be greater than or equal to 0");

    timer = watcher_new(L, sizeof(ev_timer), TIMER_MT);
    ev_timer_init(timer, &timer_cb, after, repeat);
    return 1;
}

/**
 * @see watcher_cb()
 *
 * [+0, -0, m]
 */
static void timer_cb(struct ev_loop* loop, ev_timer* timer, int revents) {
    watcher_cb(loop, timer, revents);
}

/**
 * Restart a timer with the specified repeat number of seconds.  If no
 * repeat was specified, the timer is simply stopped.  May optionally
 * specify a new value for repeat, otherwise uses the value set when
 * the timer was created.
 *
 *   1 - timer object.
 *   2 - loop object.
 *   3 - repeat (number of seconds to wait for consecutive timeouts).
 *
 * Usage:
 *    timer:again(loop [, repeat_seconds])
 *
 * [+0, -0, e]
 */
static int timer_again(lua_State *L) {
    ev_timer*       timer     = check_timer(L, 1);
    struct ev_loop* loop      = *check_loop_and_init(L, 2);
    ev_tstamp       repeat    = luaL_optnumber(L, 3, 0);

    if ( repeat < 0.0 ) luaL_argerror(L, 3, "repeat must be greater than 0");

    if ( repeat ) timer->repeat = repeat;

    if ( timer->repeat ) {
        ev_timer_again(loop, timer);
        loop_start_watcher(L, 2, 1, -1);
        timer->data = loop;
    } else {
        /* Just calling stop instead of again in case the symantics
         * change in libev */
        loop_stop_watcher(L, 2, 1);
        ev_timer_stop(loop, timer);
        timer->data = NULL;
    }

    return 0;
}

/**
 * Stops the timer so it won't be called by the specified event loop.
 *
 * Usage:
 *     timer:stop(loop)
 *
 * [+0, -0, e]
 */
static int timer_stop(lua_State *L) {
    ev_timer*       timer  = check_timer(L, 1);
    struct ev_loop* loop   = *check_loop_and_init(L, 2);

    loop_stop_watcher(L, 2, 1);
    ev_timer_stop(loop, timer);
    timer->data = NULL;

    return 0;
}

/**
 * Starts the timer so it won't be called by the specified event loop.
 *
 * Usage:
 *     timer:start(loop [, is_daemon])
 *
 * [+0, -0, e]
 */
static int timer_start(lua_State *L) {
    ev_timer*       timer  = check_timer(L, 1);
    struct ev_loop* loop   = *check_loop_and_init(L, 2);
    int is_daemon          = lua_toboolean(L, 3);

    ev_timer_start(loop, timer);
    loop_start_watcher(L, 2, 1, is_daemon);
    timer->data = loop;

    return 0;
}

/**
 * If the timer is pending, return the revents and clear the pending
 * status (so the timer callback won't be called).
 *
 * Usage:
 *   revents = timer:clear_pending(loop)
 *
 * [+1, -0, e]
 */
static int timer_clear_pending(lua_State *L) {
    ev_timer*       timer = check_timer(L, 1);
    struct ev_loop* loop   = *check_loop_and_init(L, 2);

    int revents = ev_clear_pending(loop, timer);
    if ( ! timer->repeat           &&
         ( revents & EV_TIMEOUT ) )
    {
        loop_stop_watcher(L, 2, 1);
    }

    lua_pushnumber(L, revents);
    return 1;
}

/**
 * __gc metamethod, only purpose is to stop timers when the Lua state is closed.
 *
 * lua-ev ensures that running timers are not accidentally collected, but
 * it doesn't stop timers when the lua state is closed, causing ev to possibly access
 * invalid memory. For now only timers seem to be an issue, might have to extend this
 * to other watchers in the future. Only tested with the default loop.
 *
 * See: https://github.com/brimworks/lua-ev/issues/32
 *
 * [+0, -0, e]
 */
static int timer_gc(lua_State *L) {
    ev_timer* timer = check_timer(L, 1);

    struct ev_loop* loop = timer->data;
    if (loop) {
      ev_timer_stop(loop, timer);
    }
    return 0;
}
