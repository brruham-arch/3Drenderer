script_name('b3dpet')
local ffi = require 'ffi'
local imgui = require 'mimgui'
local LOG = '/storage/emulated/0/Download/b3dlua_log.txt'
local function L(s) local f = io.open(LOG, 'a'); if f then f:write(s..'\n'); f:close() end end
local function say(s) L(s); pcall(sampAddChatMessage, '[b3d] '..s, -1) end
local function clamp(v,a,b) return math.max(a,math.min(b,v)) end
local function angDiff(a,b) return (a-b+math.pi)%(2*math.pi)-math.pi end

local lib, ready, on, errShown = nil, false, true, false
local preset = 1
local SIZE, YAWOFF = 1.0, math.pi
local FLAP = 8            -- kecepatan kepak (pose/detik), /b3dv
local BOB, PIT = 0.05, 0.15
local TILT, CLIMB, ROLL = 0.25, 0.04, 0.5
local SIDE, UP, FWD = 1.2, 1.0, -0.5   -- posisi dekat pemain, /b3dd
local WR, MAXV = 0.8, 22
local SWAY_LR, SWAY_LR_MAX, SWAY_UD, SWAY_UD_MAX = 0.35, 4.0, 0.25, 2.5

math.randomseed(os.time()); math.random(); math.random()
local pet = {x=0,y=0,z=0,vx=0,vy=0,vz=0,yaw=0,roll=0,wt=0,wox=0,woy=0,woz=0,gz=0,gt=0,seed=math.random()*10,set=false}
local P = {x=0,y=0,vx=0,vy=0,spd=0,init=false}
local T, PH = 0, 0

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
local function groundAt(x,y,zp) local a,b=getGroundZFor3dCoord(x,y,zp); if type(a)=='boolean' then if a then return b end; return nil end; if type(a)=='number' and a~=0 then return a end; return nil end

local function updatePlayer(dt)
    local x,y=getCharCoordinates(PLAYER_PED)
    if P.init then
        local vx,vy=(x-P.x)/dt,(y-P.y)/dt
        if vx*vx+vy*vy<2500 then local k=math.min(1,dt*6); P.vx=P.vx+(vx-P.vx)*k; P.vy=P.vy+(vy-P.vy)*k else P.vx,P.vy=0,0 end
    end
    P.x,P.y,P.init=x,y,true
    P.spd=math.sqrt(P.vx*P.vx+P.vy*P.vy)
end

local function fly(dt)
    updatePlayer(dt)
    local px,py,pz=getCharCoordinates(PLAYER_PED)
    local hr=math.rad(getCharHeading(PLAYER_PED))
    local fx,fy=-math.sin(hr),math.cos(hr)
    local rx,ry=fy,-fx
    local sx,sy,sz=px+rx*SIDE+fx*FWD, py+ry*SIDE+fy*FWD, pz+UP
    local pyaw=hr+math.pi/2
    local jx,jy=pet.x-px,pet.y-py
    if not pet.set or jx*jx+jy*jy>3600 then
        local a=math.random()*6.2832
        pet.x,pet.y,pet.z=px+math.cos(a)*4,py+math.sin(a)*4,pz+2.5
        pet.vx,pet.vy,pet.vz=0,0,0; pet.yaw=a+math.pi; pet.gz=pz-1; pet.set=true
    end
    pet.wt=pet.wt-dt
    if pet.wt<=0 then
        pet.wt=0.8+math.random()*1.6
        local a,r=math.random()*6.2832,math.random()*WR
        pet.wox,pet.woy,pet.woz=math.cos(a)*r,math.sin(a)*r,(math.random()-0.5)*0.8
    end
    local tx,ty,tz=sx+P.vx*0.5+pet.wox, sy+P.vy*0.5+pet.woy, sz+pet.woz
    local ddx,ddy=tx-pet.x,ty-pet.y
    local dl=math.sqrt(ddx*ddx+ddy*ddy)
    if dl>0.05 then
        local qx,qy=-ddy/dl,ddx/dl
        local amp=clamp(dl*SWAY_LR,0,SWAY_LR_MAX)
        local w=math.sin(T*1.7+pet.seed)*0.6+math.sin(T*3.9+pet.seed*2.3)*0.4
        tx=tx+qx*w*amp; ty=ty+qy*w*amp
        local v=math.sin(T*2.3+pet.seed*1.7)*0.65+math.sin(T*5.1+pet.seed)*0.35
        tz=tz+v*clamp(dl*SWAY_UD,0,SWAY_UD_MAX)
    end
    local maxv=math.min(MAXV,6+P.spd*1.6)
    local dx,dy,dz=tx-pet.x,ty-pet.y,tz-pet.z
    local d=math.sqrt(dx*dx+dy*dy+dz*dz)
    local want=math.min(maxv,d*2.4)
    local ux,uy,uz=0,0,0; if d>1e-4 then ux,uy,uz=dx/d,dy/d,dz/d end
    local a=math.min(1,dt*3.5)
    pet.vx=pet.vx+(ux*want-pet.vx)*a
    pet.vy=pet.vy+(uy*want-pet.vy)*a
    pet.vz=pet.vz+(uz*want-pet.vz)*a
    pet.vz=pet.vz+math.sin(T*3.1+pet.seed)*0.5*dt
    pet.x=pet.x+pet.vx*dt; pet.y=pet.y+pet.vy*dt; pet.z=pet.z+pet.vz*dt
    pet.gt=pet.gt-dt
    if pet.gt<=0 then pet.gt=0.25; pet.gz=groundAt(pet.x,pet.y,pet.z+5) or pet.gz end
    if pet.z<pet.gz+0.6 then pet.z=pet.gz+0.6; if pet.vz<0 then pet.vz=0 end end
    local hs=math.sqrt(pet.vx*pet.vx+pet.vy*pet.vy)
    local want_yaw=hs>1.0 and math.atan2(pet.vy,pet.vx) or pyaw
    local nyaw=pet.yaw+angDiff(want_yaw,pet.yaw)*math.min(1,5*dt)
    local yrate=angDiff(nyaw,pet.yaw)/dt
    pet.yaw=nyaw
    local rt=clamp(yrate*0.25,-1,1)*ROLL*math.min(1,hs/6)
    pet.roll=pet.roll+(rt-pet.roll)*math.min(1,dt*5)
    return math.sqrt(pet.vx*pet.vx+pet.vy*pet.vy+pet.vz*pet.vz), hs
end

local function frame()
    local io = imgui.GetIO(); local W,H = io.DisplaySize.x, io.DisplaySize.y
    local dt = clamp(io.DeltaTime, 0.001, 0.1); T = T + dt
    updateCamera(W,H)
    if not cam.ok then lib.b3d_set(0,0,0,0,0,0,0,0,1,1,0); return end
    local spd = fly(dt)
    PH = (PH + dt*FLAP*(1+math.min(spd,12)*0.05+math.max(pet.vz,0)*0.08)) % 5040
    lib.b3d_cam(cam.x,cam.y,cam.z, cam.fx,cam.fy,cam.fz, cam.rx,cam.ry, cam.ux,cam.uy,cam.uz, cam.Fx,cam.Fy, W,H)
    local bob = math.sin(T*6)*BOB
    local pit = math.cos(T*6)*PIT - math.min(spd/10,1)*TILT + clamp(pet.vz,-6,6)*CLIMB
    lib.b3d_set(pet.x,pet.y,pet.z, pet.yaw+YAWOFF, pit, pet.roll, bob, PH, SIZE, 1, on and 1 or 0)
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
        local function load(n)
            local m = find(n..'.bin')
            local x0 = find(n..'_tex0.png') or find(n..'_tex0.jpg')
            local x1 = find(n..'_tex1.png') or find(n..'_tex1.jpg') or find(n..'_tex2.png') or find(n..'_tex2.jpg') or ''
            if not (m and x0) then return false, 'file '..n..' tidak ketemu' end
            if lib.b3d_load(m, x0, x1) ~= 1 then return false, 'gagal, lihat b3d_log.txt' end
            return true
        end
        local ok1, e1 = load('phoenix'); if not ok1 then ok1, e1 = load('model') end
        if not ok1 then error(e1) end

        local function num(a,d) return tonumber(a) or d end
        sampRegisterChatCommand('b3d',  function() on = not on; say(on and 'hidup' or 'mati') end)
        sampRegisterChatCommand('b3dl', function(a) local n=(a and a~='') and a or 'model'; local ok2,e2=load(n); say(ok2 and ('dimuat: '..n) or e2) end)
        sampRegisterChatCommand('b3dm', function() preset=preset%6+1; lib.b3d_orient(preset); say('orientasi '..preset..'/6') end)
        sampRegisterChatCommand('b3dy', function(a) YAWOFF=num(a,math.pi); say('hadap '..YAWOFF) end)
        sampRegisterChatCommand('b3ds', function(a) SIZE=num(a,1); say('ukuran '..SIZE) end)
        sampRegisterChatCommand('b3dv', function(a) FLAP=num(a,8); say('kepak '..FLAP) end)
        sampRegisterChatCommand('b3dr', function(a) ROLL=num(a,0.5); say('miring belok '..ROLL) end)
        sampRegisterChatCommand('b3dw', function(a) WR=num(a,0.8); say('radius jelajah '..WR) end)
        sampRegisterChatCommand('b3dk', function(a) MAXV=num(a,22); say('kecepatan maks '..MAXV) end)
        sampRegisterChatCommand('b3dd', function(a)
            local s,u,f=tostring(a):match('(%-?[%d%.]+)%s+(%-?[%d%.]+)%s*(%-?[%d%.]*)')
            if s then SIDE,UP=tonumber(s),tonumber(u); FWD=tonumber(f) or FWD end
            say(string.format('posisi: kanan %.2f, atas %.2f, depan %.2f',SIDE,UP,FWD))
        end)
        ready = true
        say('OK. /b3dl nama /b3ds /b3dd /b3dv /b3dy /b3dm /b3d')
    end)
    if not ok then say('ERR: '..tostring(err)) end
    while true do wait(1000) end
end
