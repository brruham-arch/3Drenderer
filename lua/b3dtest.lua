script_name('b3dtest')
script_author('brruham')
local ffi = require 'ffi'
local SRC = '/storage/emulated/0/Download/libb3d.so'
local lib, on = nil, true

local function pkg()
    local t = {}
    for p in string.gmatch(getWorkingDirectory(), "([^/\\]+)") do t[#t+1] = p end
    return t[#t-1]
end

function main()
    while not isSampAvailable() do wait(100) end
    local dst = '/data/data/'..pkg()..'/libb3d.so'
    local a = io.open(SRC, 'rb'); if not a then sampAddChatMessage('[b3d] libb3d.so tidak ada', -1); return end
    local b = io.open(dst, 'wb'); if not b then a:close(); sampAddChatMessage('[b3d] gagal salin', -1); return end
    b:write(a:read('*a')); a:close(); b:close()
    ffi.cdef[[ int b3d_init(void); void b3d_enable(int on); int b3d_frames(void); ]]
    local ok, l = pcall(ffi.load, dst)
    if not ok then sampAddChatMessage('[b3d] ffi.load gagal', -1); return end
    lib = l
    if lib.b3d_init() ~= 1 then sampAddChatMessage('[b3d] init gagal, cek b3d_log.txt', -1); return end
    sampAddChatMessage('[b3d] OK. /b3d = hidup/mati segitiga', -1)
    sampRegisterChatCommand('b3d', function() on = not on; lib.b3d_enable(on and 1 or 0); sampAddChatMessage('[b3d] '..(on and 'hidup' or 'mati')..' frames='..lib.b3d_frames(), -1) end)
    wait(-1)
end
