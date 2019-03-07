
#ifndef __LUA_MARSHAL__
#define __LUA_MARSHAL__

int luaopen_marshal(lua_State *L);
int mar_decode(lua_State* L);
int mar_encode(lua_State* L);

#endif
