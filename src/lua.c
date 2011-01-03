#include "redis.h"
#include "lua.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

lua_State *luaState;

static void *luaZalloc (void *ud, void *ptr, size_t osize, size_t nsize) {
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        zfree(ptr);
        return NULL;
    }
    else
        return zrealloc(ptr, nsize);
}

static int luaPanic (lua_State *L) {
    (void)L;  /* to avoid warnings */
    redisPanic(sprintf("PANIC: unprotected error in call to Lua API (%s)\n",
                lua_tostring(L, -1)));
    return 0;
}

void initLua(void) {
    luaState = lua_newstate(luaZalloc, NULL);
    if (!luaState) {
        redisPanic("Unable to initialize Lua!");
    }
    lua_atpanic(luaState, &luaPanic);
    luaL_openlibs(luaState);
}

void luaexecCommand(redisClient *c) {
    luaGenericExecCommand(c, c->argv[1]->ptr);
}

void luaexeckeyCommand(redisClient *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL) {
        return;
    }

    if (o->type != REDIS_STRING) {
        addReply(c,shared.wrongtypeerr);
        return;
    }

    luaGenericExecCommand(c, o->ptr);
}

void luaGenericExecCommand(redisClient *c, const char *code) {
    int err;

    if ((err = luaL_loadstring(luaState, code))) {
        if (err == LUA_ERRSYNTAX) {
            addReply(c, shared.syntaxerr);
        } else if (err == LUA_ERRMEM) {
            addReply(c, createObject(REDIS_STRING,sdsnew("-ERR Memory allocation error in LUA script")));
        }
        return;
    }

    if ((err = lua_pcall(luaState, 0, LUA_MULTRET, 0))) {
        sds luamsg = sdsnew("-ERR Lua error: ");
        luamsg = sdscat(luamsg, lua_tostring(luaState, -1));
        addReply(c, createObject(REDIS_STRING, luamsg));
        return;
    }
}
