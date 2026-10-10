script_name('b3dtest')
local ffi = require 'ffi'
local imgui = require 'mimgui'
local LOG = '/storage/emulated/0/Download/b3dlua_log.txt'
local function L(s) local f = io.open(LOG, 'a'); if f then f:write(s..'\n'); f:close() end end
local function say(s) L(s); pcall(sampAddChatMessage, '[b3d] '..s, -1) end

local lib, ready, on, errShown = nil, false, true, false
local SIZE, YAWOFF, preset = 1.0, math.pi, 1

local function projNative(x,y,z) local a,b,c=convert3DCoordsToScreen(x,y,z); if type(a)=='boolean' then if not a then return nil end; return b,c end; return a,b end
local cam={ok=false}
local function updateCamera(W,H)
    cam.ok=false; local cx,cy,cz=getActiveCameraCoordinates(); local tx,ty,tz=getActiveCameraPointAt(); if not (cx and tx) then return end
    local fx,fy,fz=tx-cx,ty-cy,tz-cz; local fl=math.sqrt(fx*fx+fy*fy+fz*fz); if fl<1e-4 then return end; fx,fy,fz=fx/fl,fy/fl,fz/fl
    local rx,ry=fy,-fx; local rl=math.sqrt(rx*rx+ry*ry); if rl<1e-3 then return end; rx,ry=rx/rl,ry/rl
    local ux,uy,uz=ry*fz,-rx*fz,rx*fy-ry*fx; local D=10; local px,py,pz=cx+fx*D,cy+fy*D,cz+fz*D
    local c0x,c0y=projNative(px,py,pz); local r1x,r1y=projNative(px+rx,py+ry,pz); local u1x,u1y=projNative(px+ux,py+uy,pz+uz)
    if not (c0x and r1x and u1x) or c0x<=1 or c0y<=1 then return end; local sx,sy=W/(2*c0x),H/(2*c0y)
    cam.x,cam.y,cam.z=cx,cy,cz; cam.fx,cam.fy,cam.fz=fx,fy,fz; cam.rx,cam.ry=rx,ry; cam.ux,cam.uy,cam.uz=ux,uy,uz
    cam.Fx=(r1x-c0x)*D*sx; cam.Fy=(u1y-c0y)*D*sy; cam.W,cam.H=W,H; cam.ok=true
end

local function frame()
    local io = imgui.GetIO(); local W,H = io.DisplaySize.x, io.DisplaySize.y
    updateCamera(W,H)
    if not cam.ok then lib.b3d_set(0,0,0,0,0,0,0,0,1,1,0); return end
    lib.b3d_cam(cam.x,cam.y,cam.z, cam.fx,cam.fy,cam.fz, cam.rx,cam.ry, cam.ux,cam.uy,cam.uz, cam.Fx,cam.Fy, W,H)
    local px,py,pz = getCharCoordinates(PLAYER_PED)
    local hr = math.rad(getCharHeading(PLAYER_PED))
    local bx,by = px - math.sin(hr)*3, py + math.cos(hr)*3
    lib.b3d_set(bx,by,pz+0.3, hr+math.pi/2+YAWOFF, 0,0,0, os.clock()*8, SIZE, 1, on and 1 or 0)
end

local sub = imgui.OnFrame(function() return ready end, function()
    local ok,e = pcall(frame)
    if not ok and not errShown then errShown = true; say('ERR frame: '..tostring(e)) end
end)
sub.HideCursor = true; sub.LockPlayer = false

function main()
    os.remove(LOG)
    while not isSampAvailable() do wait(100) end
    local ok, err = pcall(function()
        local SRC = '/storage/emulated/0/Download/libb3d.so'
        local t = {}
        for p in string.gmatch(getWorkingDirectory(), "([^/\\]+)") do t[#t+1] = p end
        local dst = '/data/data/'..tostring(t[#t-1])..'/libb3d.so'
        local a = io.open(SRC, 'rb'); if not a then error('libb3d.so tidak ada di Download') end
        local b = io.open(dst, 'wb'); if not b then a:close(); error('gagal tulis '..dst) end
        b:write(a:read('*a')); a:close(); b:close()
        pcall(ffi.cdef, [[
            int b3d_init(void); int b3d_load(const char* m, const char* t0, const char* t1);
            void b3d_orient(int p);
            void b3d_cam(float cx,float cy,float cz,float fx,float fy,float fz,float rx,float ry,float ux,float uy,float uz,float Fx,float Fy,float W,float H);
            void b3d_set(float x,float y,float z,float yaw,float pitch,float roll,float bob,float phase,float size,float alpha,int vis);
            void b3d_enable(int on); int b3d_frames(void);
        ]])
        lib = ffi.load(dst)
        if lib.b3d_init() ~= 1 then error('b3d_init gagal, lihat b3d_log.txt') end

        local dirs = {getWorkingDirectory()..'/assets/images/', '/storage/emulated/0/Download/'}
        local function find(n) for _,d in ipairs(dirs) do if doesFileExist(d..n) then return d..n end end end
        local mp = find('model.bin'); if not mp then error('model.bin tidak ketemu') end
        local t0 = find('model_tex0.jpg') or find('model_tex0.png'); if not t0 then error('model_tex0 tidak ketemu') end
        local t1 = find('model_tex1.jpg') or find('model_tex1.png') or ''
        L('model='..mp..' tex0='..t0)
        if lib.b3d_load(mp, t0, t1) ~= 1 then error('b3d_load gagal, lihat b3d_log.txt') end

        sampRegisterChatCommand('b3d',  function() on = not on; say(on and 'hidup' or 'mati') end)
        sampRegisterChatCommand('b3dm', function() preset = preset % 6 + 1; lib.b3d_orient(preset); say('orientasi '..preset..'/6') end)
        sampRegisterChatCommand('b3dy', function(a) YAWOFF = tonumber(a) or math.pi; say('yawoff '..YAWOFF) end)
        sampRegisterChatCommand('b3ds', function(a) SIZE = tonumber(a) or 1; say('size '..SIZE) end)
        sampRegisterChatCommand('b3dl', function(a)
            local n = (a and a ~= '') and a or 'model'
            local m = find(n..'.bin')
            local x0 = find(n..'_tex0.png') or find(n..'_tex0.jpg')
            local x1 = find(n..'_tex1.png') or find(n..'_tex1.jpg') or find(n..'_tex2.png') or find(n..'_tex2.jpg') or ''
            if not (m and x0) then say('file '..n..' tidak ketemu'); return end
            say(lib.b3d_load(m, x0, x1) == 1 and ('dimuat: '..n) or 'gagal, lihat b3d_log.txt')
        end)
        ready = true
        say('OK. /b3d /b3dm /b3dy /b3ds')
    end)
    if not ok then say('ERR: '..tostring(err)) end
    while true do wait(1000) end
end
