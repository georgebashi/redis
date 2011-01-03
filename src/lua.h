#ifndef __LUA_H
#define __LUA_H

#include "redis.h"

void initLua(void);
void luaGenericExecCommand(redisClient *c, const char *);

#endif
