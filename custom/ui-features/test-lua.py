import ctypes as c, pathlib, sys
lua=c.CDLL('liblua5.4.so.0')
lua.luaL_newstate.restype=c.c_void_p
lua.luaL_openlibs.argtypes=[c.c_void_p]
lua.luaL_loadfilex.argtypes=[c.c_void_p,c.c_char_p,c.c_char_p]
lua.lua_pcallk.argtypes=[c.c_void_p,c.c_int,c.c_int,c.c_int,c.c_longlong,c.c_void_p]
lua.lua_tolstring.argtypes=[c.c_void_p,c.c_int,c.c_void_p];lua.lua_tolstring.restype=c.c_char_p
lua.lua_close.argtypes=[c.c_void_p]
root=pathlib.Path('custom/ui-features')
for file in list((root/'InventoryExtenderIntegration/Data_Files').rglob('*.lua'))+[root/'world-tooltip-tests.lua', root/'inventory-quality-tests.lua', root/'icon-pack-tests.lua', root/'recent-hover-tests.lua']:
 s=lua.luaL_newstate();lua.luaL_openlibs(s)
 status=lua.luaL_loadfilex(s,str(file).encode(),None)
 if not status and file.name in ('world-tooltip-tests.lua', 'inventory-quality-tests.lua', 'icon-pack-tests.lua', 'recent-hover-tests.lua'):status=lua.lua_pcallk(s,0,0,0,0,None)
 if status:raise RuntimeError(lua.lua_tolstring(s,-1,None).decode())
 lua.lua_close(s)
print('PASS: shipped Lua syntax (Lua 5.4); not a full OpenMW/LuaJIT runtime test')

