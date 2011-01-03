#include "redis.h"
#include "lua.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

lua_State *luaState;
redisClient *fakeClient;

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
    fakeClient = zmalloc(sizeof(redisClient));
    selectDb(fakeClient, 0);
    fakeClient->flags = REDIS_LUA;
    fakeClient->returned_values = 0;
    fakeClient->cmdState = NULL;

    luaState = lua_newstate(luaZalloc, NULL);
    if (!luaState) {
        redisPanic("Unable to initialize Lua!");
    }
    lua_atpanic(luaState, &luaPanic);
    luaL_openlibs(luaState);

    lua_pushcfunction(luaState, luaExecRedisCommand);
    lua_setglobal(luaState, "redis");
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

    c->returned_values = 0;

    if ((err = luaL_loadstring(luaState, code))) {
        if (err == LUA_ERRSYNTAX) {
            addReply(c, shared.syntaxerr);
        } else if (err == LUA_ERRMEM) {
            addReply(c, createObject(REDIS_STRING,sdsnew("-ERR Memory allocation error in LUA script")));
        }
        return;
    }

    sds output;
    if ((err = lua_pcall(luaState, 0, 1, 0))) {
        output = sdsnew("-ERR Lua error: ");
        output = sdscat(output, lua_tostring(luaState, -1));
    } else {
        output = sdsnew(lua_tostring(luaState, -1));
    }
    addReply(c,shared.plus);
    addReply(c, createObject(REDIS_STRING, output));
    addReply(c,shared.crlf);
}

int luaExecRedisCommand(lua_State *L) {
    int nargs = lua_gettop(L);
    const char *cmdname = lua_tostring(L, 1);

    struct redisCommand *cmd = lookupCommandByCString(cmdname);
    if (nargs != cmd->arity) {
        lua_pushstring(L, "incorrect number of arguments");
        lua_error(L);
    }
    fakeClient->argc = nargs;
    fakeClient->argv = zmalloc(sizeof(robj) * nargs);
    for (int i = 0; i < nargs; i++) {
        const char *param = lua_tostring(L, i + 1);
        fakeClient->argv[i] = createStringObject(param, strlen(param));
    }
    fakeClient->cmdState = L;
    lua_pop(L, nargs);
    fakeClient->returned_values = 0;

    call(fakeClient, cmd);
    for (int i = 0; i < nargs; i++) {
        decrRefCount(fakeClient->argv[i]);
    }
    zfree(fakeClient->argv);
    fakeClient->argv = NULL;

    return fakeClient->returned_values;
}

void luaReturnObject(redisClient *c, robj *obj) {
    robj *decoded = getDecodedObject(obj);

    if (obj->type == REDIS_STRING) {
        lua_pushstring(c->cmdState, decoded->ptr);
    } else {
        redisLog(REDIS_NOTICE, "unhandled type");
    }
    decrRefCount(decoded);

    c->returned_values++;
}

