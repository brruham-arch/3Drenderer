// libb3d.so - engine render 3D native GLES2 (M2: model dari luar, tekstur, kamera, animasi GPU)
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <vector>
#include <mutex>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#include "stb_image.h"

#define EXPORT extern "C" __attribute__((visibility("default")))
#define LOGFILE "/storage/emulated/0/Android/media/com.nezukosamp.game/b3d_log.txt"

static void blog(const char* fmt, ...) {
    char b[512]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    __android_log_print(ANDROID_LOG_INFO, "libb3d", "%s", b);
    FILE* f = fopen(LOGFILE, "a"); if (f) { fprintf(f, "%s\n", b); fclose(f); }
}

static bool gotHook(void* a, void* repl, void** orig) {
    *orig = a;
    uintptr_t tgt = (uintptr_t)a & ~(uintptr_t)1;
    FILE* m = fopen("/proc/self/maps", "r");
    if (!m) return false;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, m)) {
        if (!strstr(line, "libGTASA.so") && !strstr(line, "libAlyn_SAMPMOBILE.so")) continue;
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3) continue;
        if (perm[0] != 'r' || perm[2] == 'x') continue;
        int prot = PROT_READ | (perm[1] == 'w' ? PROT_WRITE : 0);
        for (uintptr_t p = s; p + sizeof(uintptr_t) <= e; p += sizeof(uintptr_t)) {
            if ((*(uintptr_t*)p & ~(uintptr_t)1) == tgt) {
                uintptr_t pg = p & ~((uintptr_t)sysconf(_SC_PAGESIZE) - 1);
                mprotect((void*)pg, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE);
                *(uintptr_t*)p = (uintptr_t)repl;
                mprotect((void*)pg, sysconf(_SC_PAGESIZE), prot);
                n++;
            }
        }
    }
    fclose(m);
    return n > 0;
}

// ---------- data model (CPU) ----------
struct Model {
    int NF = 0, nv = 0, nf = 0, nfA = 0;
    std::vector<float> uv, fr, nm;
    std::vector<uint16_t> fc;
    std::vector<unsigned char> px0, px1;
    int w0 = 0, h0 = 0, w1 = 0, h1 = 0;
    int ver = 0;
};
static Model M;
static std::mutex mu;
static int verCounter = 0;

static const int PRE[6][3][2] = {
    {{1,1},{3,-1},{2,1}}, {{1,-1},{3,1},{2,1}},
    {{1,1},{2,1},{3,1}},  {{1,-1},{2,-1},{3,1}},
    {{3,1},{1,1},{2,1}},  {{3,-1},{1,-1},{2,1}},
};
static int preset = 1;
struct Cam { float cx, cy, cz, fx, fy, fz, rx, ry, ux, uy, uz, Fx, Fy, W, H; };
static Cam C; static int camOk = 0;
struct Obj { float x, y, z, yaw, pitch, roll, bob, phase, size, alpha; };
static Obj O = {0,0,0,0,0,0,0,0,1,1}; static int visible = 0;

static bool readFile(const char* p, std::vector<unsigned char>& out) {
    FILE* f = fopen(p, "rb"); if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    out.resize(n);
    size_t r = fread(out.data(), 1, n, f); fclose(f);
    return r == (size_t)n;
}

static bool loadTex(const char* path, std::vector<unsigned char>& px, int& w, int& h) {
    int c = 0;
    unsigned char* d = stbi_load(path, &w, &h, &c, 4);
    if (!d) { blog("tekstur gagal: %s", path); return false; }
    px.assign(d, d + (size_t)w * h * 4);
    stbi_image_free(d);
    return true;
}

static void calcNormals(Model& m) {
    m.nm.assign((size_t)m.NF * m.nv * 3, 0.f);
    for (int p = 0; p < m.NF; p++) {
        const float* P = &m.fr[(size_t)p * m.nv * 3];
        float* N = &m.nm[(size_t)p * m.nv * 3];
        for (int f = 0; f < m.nf; f++) {
            int a = m.fc[f*3], b = m.fc[f*3+1], c = m.fc[f*3+2];
            if (a >= m.nv || b >= m.nv || c >= m.nv) continue;
            float ux = P[b*3]-P[a*3], uy = P[b*3+1]-P[a*3+1], uz = P[b*3+2]-P[a*3+2];
            float vx = P[c*3]-P[a*3], vy = P[c*3+1]-P[a*3+1], vz = P[c*3+2]-P[a*3+2];
            float nx = uy*vz-uz*vy, ny = uz*vx-ux*vz, nz = ux*vy-uy*vx;
            int ids[3] = {a, b, c};
            for (int k = 0; k < 3; k++) { N[ids[k]*3] += nx; N[ids[k]*3+1] += ny; N[ids[k]*3+2] += nz; }
        }
        for (int v = 0; v < m.nv; v++) {
            float x = N[v*3], y = N[v*3+1], z = N[v*3+2];
            float l = sqrtf(x*x + y*y + z*z);
            if (l < 1e-12f) { N[v*3] = 0; N[v*3+1] = 0; N[v*3+2] = 1; }
            else { N[v*3] = x/l; N[v*3+1] = y/l; N[v*3+2] = z/l; }
        }
    }
}

// ---------- GPU ----------
static GLuint prog = 0, vbPos = 0, vbNrm = 0, vbUv = 0, ibo = 0, tex0 = 0, tex1 = 0;
static int gpuVer = -1, hasTex1 = 0;
static GLint uRot, uPos, uSize, uU, uF, uR, uUp, uFxy, uHalf, uZ, uTex, uAlpha;

static const char* VS = R"GLSL(
attribute vec3 aP0; attribute vec3 aP1; attribute vec3 aN0; attribute vec3 aN1; attribute vec2 aUV;
uniform mat3 uRot; uniform vec3 uPos; uniform float uSize; uniform float uU;
uniform vec3 uF; uniform vec3 uR; uniform vec3 uUp;
uniform vec2 uFxy; uniform vec2 uHalf; uniform vec2 uZ;
varying vec2 vUv; varying float vSh;
void main() {
    vec3 m = mix(aP0, aP1, uU);
    vec3 n = mix(aN0, aN1, uU);
    vec3 d = uPos + (uRot * m) * uSize;
    vec3 nw = uRot * n;
    float l = length(nw);
    nw = (l > 0.0001) ? nw / l : vec3(0.0, 0.0, 1.0);
    vSh = 0.45 + 0.55 * abs(dot(nw, vec3(0.40562, 0.30421, 0.86193)));
    float zc = dot(d, uF);
    float xc = dot(d, uR);
    float yc = dot(d, uUp);
    vUv = aUV;
    gl_Position = vec4(xc * uFxy.x / uHalf.x, -yc * uFxy.y / uHalf.y, uZ.x * zc - uZ.y, zc);
}
)GLSL";

static const char* FS = R"GLSL(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 vUv; varying float vSh;
uniform sampler2D uTex; uniform float uAlpha;
void main() {
    vec4 c = texture2D(uTex, vUv);
    if (c.a < 0.1) discard;
    gl_FragColor = vec4(c.rgb * vSh, uAlpha);
}
)GLSL";

static GLuint compile(GLenum k, const char* s) {
    GLuint sh = glCreateShader(k);
    glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[512]; glGetShaderInfoLog(sh, 512, nullptr, b); blog("shader gagal: %s", b); return 0; }
    return sh;
}

static bool build() {
    GLuint v = compile(GL_VERTEX_SHADER, VS), f = compile(GL_FRAGMENT_SHADER, FS);
    if (!v || !f) return false;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glBindAttribLocation(p, 0, "aP0"); glBindAttribLocation(p, 1, "aP1");
    glBindAttribLocation(p, 2, "aN0"); glBindAttribLocation(p, 3, "aN1");
    glBindAttribLocation(p, 4, "aUV");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char b[512]; glGetProgramInfoLog(p, 512, nullptr, b); blog("link gagal: %s", b); return false; }
    prog = p;
    uRot = glGetUniformLocation(p, "uRot"); uPos = glGetUniformLocation(p, "uPos");
    uSize = glGetUniformLocation(p, "uSize"); uU = glGetUniformLocation(p, "uU");
    uF = glGetUniformLocation(p, "uF"); uR = glGetUniformLocation(p, "uR");
    uUp = glGetUniformLocation(p, "uUp"); uFxy = glGetUniformLocation(p, "uFxy");
    uHalf = glGetUniformLocation(p, "uHalf"); uZ = glGetUniformLocation(p, "uZ");
    uTex = glGetUniformLocation(p, "uTex"); uAlpha = glGetUniformLocation(p, "uAlpha");
    blog("shader siap");
    return true;
}

static GLuint makeTex(const std::vector<unsigned char>& px, int w, int h) {
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    return t;
}

static void upload() {
    if (vbPos) {
        GLuint b[4] = {vbPos, vbNrm, vbUv, ibo}; glDeleteBuffers(4, b);
        GLuint t[2] = {tex0, tex1}; glDeleteTextures(2, t);
    }
    glGenBuffers(1, &vbPos); glBindBuffer(GL_ARRAY_BUFFER, vbPos);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(M.fr.size() * 4), M.fr.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &vbNrm); glBindBuffer(GL_ARRAY_BUFFER, vbNrm);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(M.nm.size() * 4), M.nm.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &vbUv); glBindBuffer(GL_ARRAY_BUFFER, vbUv);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(M.uv.size() * 4), M.uv.data(), GL_STATIC_DRAW);
    glGenBuffers(1, &ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(M.fc.size() * 2), M.fc.data(), GL_STATIC_DRAW);
    tex0 = makeTex(M.px0, M.w0, M.h0);
    hasTex1 = !M.px1.empty();
    tex1 = hasTex1 ? makeTex(M.px1, M.w1, M.h1) : 0;
    gpuVer = M.ver;
    blog("upload GPU: %d vertex, %d segitiga, %d pose", M.nv, M.nf, M.NF);
}

static void mul3(const float* a, const float* b, float* o) {
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        o[i*3+j] = a[i*3]*b[j] + a[i*3+1]*b[3+j] + a[i*3+2]*b[6+j];
}

static const GLenum CAPS[5] = { GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST };
struct Attr { GLint en, buf, size, type, norm, stride; void* ptr; };

static void drawBird(EGLDisplay d, EGLSurface s) {
    EGLint w = 0, h = 0;
    eglQuerySurface(d, s, EGL_WIDTH, &w); eglQuerySurface(d, s, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;
    std::lock_guard<std::mutex> lk(mu);
    if (M.nv == 0 || !prog) return;

    GLint oFbo, oProg, oAct, oArr, oEl, vp[4], oTex, oDF, bsR, bdR, bsA, bdA;
    GLboolean oDM, saved[5]; GLfloat oCD;
    Attr at[5];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oFbo); glGetIntegerv(GL_CURRENT_PROGRAM, &oProg);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oAct);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oArr); glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &oEl);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_DEPTH_FUNC, &oDF); glGetBooleanv(GL_DEPTH_WRITEMASK, &oDM); glGetFloatv(GL_DEPTH_CLEAR_VALUE, &oCD);
    glGetIntegerv(GL_BLEND_SRC_RGB, &bsR); glGetIntegerv(GL_BLEND_DST_RGB, &bdR);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsA); glGetIntegerv(GL_BLEND_DST_ALPHA, &bdA);
    for (int i = 0; i < 5; i++) saved[i] = glIsEnabled(CAPS[i]);
    for (int i = 0; i < 5; i++) {
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &at[i].en);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &at[i].buf);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &at[i].size);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &at[i].type);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &at[i].norm);
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &at[i].stride);
        glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &at[i].ptr);
    }
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D, &oTex);

    if (gpuVer != M.ver) upload();

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    for (int i = 0; i < 5; i++) glDisable(CAPS[i]);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDepthMask(GL_TRUE);
    glClearDepthf(1.0f); glClear(GL_DEPTH_BUFFER_BIT);
    if (O.alpha < 0.999f) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }

    // matriks rotasi total = W * P * R * Pre  (lokal -> dunia)
    float Pre[9] = {0,0,0,0,0,0,0,0,0};
    for (int k = 0; k < 3; k++) Pre[k*3 + (PRE[preset-1][k][0]-1)] = (float)PRE[preset-1][k][1];
    float cr = cosf(O.roll), sr = sinf(O.roll), cp = cosf(O.pitch), sp = sinf(O.pitch);
    float cy = cosf(O.yaw), sn = sinf(O.yaw);
    float R[9] = {cr,0,-sr, 0,1,0, sr,0,cr};
    float Pm[9] = {1,0,0, 0,cp,-sp, 0,sp,cp};
    float Wm[9] = {sn,cy,0, -cy,sn,0, 0,0,1};
    float t1[9], t2[9], T[9], Tc[9];
    mul3(R, Pre, t1); mul3(Pm, t1, t2); mul3(Wm, t2, T);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Tc[c*3+r] = T[r*3+c];

    float ph = fmodf(O.phase, (float)M.NF); if (ph < 0) ph += M.NF;
    int i0 = (int)ph; if (i0 >= M.NF) i0 = 0;
    float u = ph - i0; int i1 = (i0 + 1) % M.NF;
    const float zn = 0.3f, zf = 400.0f;

    glUseProgram(prog);
    glUniformMatrix3fv(uRot, 1, GL_FALSE, Tc);
    glUniform3f(uPos, O.x - C.cx, O.y - C.cy, O.z + O.bob - C.cz);
    glUniform1f(uSize, O.size); glUniform1f(uU, u);
    glUniform3f(uF, C.fx, C.fy, C.fz); glUniform3f(uR, C.rx, C.ry, 0.0f); glUniform3f(uUp, C.ux, C.uy, C.uz);
    glUniform2f(uFxy, C.Fx, C.Fy); glUniform2f(uHalf, C.W * 0.5f, C.H * 0.5f);
    glUniform2f(uZ, (zf + zn) / (zf - zn), 2.0f * zf * zn / (zf - zn));
    glUniform1i(uTex, 0); glUniform1f(uAlpha, O.alpha);

    size_t st = (size_t)M.nv * 12;
    for (int i = 0; i < 5; i++) glEnableVertexAttribArray(i);
    glBindBuffer(GL_ARRAY_BUFFER, vbPos);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void*)(i0 * st));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, (const void*)(i1 * st));
    glBindBuffer(GL_ARRAY_BUFFER, vbNrm);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 0, (const void*)(i0 * st));
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, 0, (const void*)(i1 * st));
    glBindBuffer(GL_ARRAY_BUFFER, vbUv);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, 0, (const void*)0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    if (M.nfA > 0) {
        glBindTexture(GL_TEXTURE_2D, tex0);
        glDrawElements(GL_TRIANGLES, M.nfA * 3, GL_UNSIGNED_SHORT, (const void*)0);
    }
    if (M.nf > M.nfA) {
        glBindTexture(GL_TEXTURE_2D, hasTex1 ? tex1 : tex0);
        glDrawElements(GL_TRIANGLES, (M.nf - M.nfA) * 3, GL_UNSIGNED_SHORT, (const void*)(size_t)(M.nfA * 6));
    }
    // ---- pulihkan state ----
    for (int i = 0; i < 5; i++) {
        glBindBuffer(GL_ARRAY_BUFFER, at[i].buf);
        if (at[i].size > 0)
            glVertexAttribPointer(i, at[i].size, at[i].type, (GLboolean)at[i].norm, at[i].stride, at[i].ptr);
        if (at[i].en) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
    }
    glBindBuffer(GL_ARRAY_BUFFER, oArr); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, oEl);
    glUseProgram(oProg);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, oTex); glActiveTexture(oAct);
    glBlendFuncSeparate(bsR, bdR, bsA, bdA);
    for (int i = 0; i < 5; i++) { if (saved[i]) glEnable(CAPS[i]); else glDisable(CAPS[i]); }
    glDepthFunc(oDF); glDepthMask(oDM); glClearDepthf(oCD);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, oFbo);
}

// ---------- hook ----------
typedef EGLBoolean (*PFN_Swap)(EGLDisplay, EGLSurface);
static PFN_Swap o_Swap = nullptr;
static void* curCtx = nullptr;
static int enabled = 1, frames = 0, inited = 0;

static EGLBoolean h_Swap(EGLDisplay d, EGLSurface s) {
    if (!o_Swap) return EGL_TRUE;
    void* c = eglGetCurrentContext();
    if (c) {
        if (c != curCtx) {
            curCtx = c; prog = 0; gpuVer = -1;
            vbPos = vbNrm = vbUv = ibo = tex0 = tex1 = 0;
            blog("context baru, buat ulang resource");
        }
        if (!prog) build();
        if (frames++ == 0) blog("frame pertama");
        if (enabled && visible && camOk) drawBird(d, s);
    }
    return o_Swap(d, s);
}

// ---------- API ----------
EXPORT int b3d_init(void) {
    if (inited) return 1;
    remove(LOGFILE);
    void* hEGL = dlopen("libEGL.so", RTLD_NOW);
    if (!hEGL) { blog("ERROR dlopen libEGL"); return -1; }
    void* a = dlsym(hEGL, "eglSwapBuffers");
    if (!a) { blog("ERROR sym eglSwapBuffers"); return -2; }
    void* hD = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
    int (*dh)(void*, void*, void**) = hD ? (int (*)(void*, void*, void**))dlsym(hD, "DobbyHook") : nullptr;
    if (dh) {
        if (dh(a, (void*)h_Swap, (void**)&o_Swap) != 0) { blog("ERROR DobbyHook gagal"); return -3; }
        blog("hook via Dobby");
    } else if (!gotHook(a, (void*)h_Swap, (void**)&o_Swap)) { blog("ERROR libdobby.so tidak ada & GOT gagal"); return -4; }
    inited = 1;
    blog("b3d_init OK (M2)");
    return 1;
}

// model.bin (PHX1) + tekstur; tex1 boleh null/kosong
EXPORT int b3d_load(const char* modelPath, const char* t0, const char* t1) {
    Model* nm = new Model();
    std::vector<unsigned char> b;
    if (!modelPath || !readFile(modelPath, b) || b.size() < 24 || memcmp(b.data(), "PHX1", 4) != 0) {
        blog("model gagal dibaca: %s", modelPath ? modelPath : "(null)"); delete nm; return 0;
    }
    uint32_t h[5]; memcpy(h, b.data() + 4, 20);
    nm->NF = h[0]; nm->nv = h[1]; nm->nf = h[2]; nm->nfA = h[3];
    size_t need = 24 + (size_t)nm->nv * 8 + (size_t)nm->nf * 6 + (size_t)nm->NF * nm->nv * 12;
    if (nm->NF < 1 || nm->nv < 1 || nm->nf < 1 || b.size() < need) { blog("model.bin rusak/terpotong"); delete nm; return 0; }
    size_t o = 24;
    nm->uv.resize((size_t)nm->nv * 2); memcpy(nm->uv.data(), b.data() + o, (size_t)nm->nv * 8); o += (size_t)nm->nv * 8;
    nm->fc.resize((size_t)nm->nf * 3); memcpy(nm->fc.data(), b.data() + o, (size_t)nm->nf * 6); o += (size_t)nm->nf * 6;
    nm->fr.resize((size_t)nm->NF * nm->nv * 3); memcpy(nm->fr.data(), b.data() + o, (size_t)nm->NF * nm->nv * 12);
    if (!t0 || !loadTex(t0, nm->px0, nm->w0, nm->h0)) { delete nm; return 0; }
    if (nm->nfA < nm->nf && t1 && t1[0]) loadTex(t1, nm->px1, nm->w1, nm->h1);
    calcNormals(*nm);
    {
        std::lock_guard<std::mutex> lk(mu);
        nm->ver = ++verCounter;
        M = std::move(*nm);
    }
    delete nm;
    blog("model dimuat: %d vertex, %d segitiga, %d pose, tekstur %dx%d", M.nv, M.nf, M.NF, M.w0, M.h0);
    return 1;
}
EXPORT void b3d_orient(int p) { if (p >= 1 && p <= 6) preset = p; }
EXPORT void b3d_cam(float cx, float cy, float cz, float fx, float fy, float fz, float rx, float ry,
                    float ux, float uy, float uz, float Fx, float Fy, float W, float H) {
    C = {cx, cy, cz, fx, fy, fz, rx, ry, ux, uy, uz, Fx, Fy, W, H}; camOk = 1;
}
EXPORT void b3d_set(float x, float y, float z, float yaw, float pitch, float roll, float bob,
                    float phase, float size, float alpha, int vis) {
    O = {x, y, z, yaw, pitch, roll, bob, phase, size, alpha}; visible = vis;
}
EXPORT void b3d_enable(int on) { enabled = on; }
EXPORT int  b3d_frames(void) { return frames; }
