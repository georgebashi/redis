#ifndef __LUA_H
#define __LUA_H

#include <lua.h>
#include "redis.h"

void initLua(void);
void luaGenericExecCommand(redisClient *c, const char *);
int luaExecRedisCommand(lua_State *L);
void luaReturnBuffer(redisClient *c, char *s, size_t len);

#endif
