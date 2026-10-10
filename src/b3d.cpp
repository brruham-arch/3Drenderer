// libb3d.so - engine render 3D native (M1: hook eglSwapBuffers + segitiga uji)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define LOGFILE "/storage/emulated/0/Download/b3d_log.txt"

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
        if (!strstr(line, "libGTASA.so")) continue;
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3) continue;
        if (perm[0] != 'r' || perm[2] == 'x') continue;
        int prot = PROT_READ | (perm[1] == 'w' ? PROT_WRITE : 0);
        for (uintptr_t p = s; p + 4 <= e; p += 4) {
            if ((*(uintptr_t*)p & ~(uintptr_t)1) == tgt) {
                uintptr_t pg = p & ~(uintptr_t)4095;
                mprotect((void*)pg, 4096, PROT_READ | PROT_WRITE);
                *(uintptr_t*)p = (uintptr_t)repl;
                mprotect((void*)pg, 4096, prot);
                n++;
            }
        }
    }
    fclose(m);
    return n > 0;
}

typedef EGLBoolean (*PFN_Swap)(EGLDisplay, EGLSurface);
static PFN_Swap o_Swap = nullptr;
static void* curCtx = nullptr;
static GLuint prog = 0;
static GLint uCol = -1;
static int enabled = 1, frames = 0, inited = 0, errLogged = 0;

static const char* VS = "attribute vec2 aPos; void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }";
static const char* FS = "precision mediump float; uniform vec4 uCol; void main(){ gl_FragColor = uCol; }";

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
    prog = glCreateProgram();
    glAttachShader(prog, v); glAttachShader(prog, f);
    glBindAttribLocation(prog, 0, "aPos"); glLinkProgram(prog);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char b[512]; glGetProgramInfoLog(prog, 512, nullptr, b); blog("link gagal: %s", b); prog = 0; return false; }
    uCol = glGetUniformLocation(prog, "uCol");
    blog("shader siap");
    return true;
}

static const GLenum CAPS[5] = { GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST, GL_STENCIL_TEST };
static const float tri[6] = { -0.2f, -0.2f, 0.2f, -0.2f, 0.0f, 0.25f };

static void drawTest(EGLDisplay d, EGLSurface s) {
    EGLint w = 0, h = 0;
    eglQuerySurface(d, s, EGL_WIDTH, &w); eglQuerySurface(d, s, EGL_HEIGHT, &h);
    if (w <= 0 || h <= 0) return;

    GLint oFbo, oProg, oAct, oBuf, vp[4], oAttr;
    GLboolean saved[5];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oProg);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oAct);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oBuf);
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &oAttr);
    for (int i = 0; i < 5; i++) saved[i] = glIsEnabled(CAPS[i]);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    for (int i = 0; i < 5; i++) glDisable(CAPS[i]);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(prog);
    glUniform4f(uCol, 1.0f, 0.5f, 0.0f, 1.0f);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (!errLogged) { GLenum e = glGetError(); if (e) { blog("GL error 0x%04X", e); errLogged = 1; } }

    if (oAttr == 0) glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, oBuf);
    glUseProgram(oProg);
    glActiveTexture(oAct);
    for (int i = 0; i < 5; i++) { if (saved[i]) glEnable(CAPS[i]); else glDisable(CAPS[i]); }
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glBindFramebuffer(GL_FRAMEBUFFER, oFbo);
}

static EGLBoolean h_Swap(EGLDisplay d, EGLSurface s) {
    if (!o_Swap) return EGL_TRUE;
    void* c = eglGetCurrentContext();
    if (c) {
        if (c != curCtx) { curCtx = c; prog = 0; blog("context baru, buat ulang resource"); }
        if (!prog) build();
        if (frames++ == 0) blog("frame pertama");
        if (enabled && prog) drawTest(d, s);
    }
    return o_Swap(d, s);
}

EXPORT int b3d_init(void) {
    if (inited) return 1;
    remove(LOGFILE);
    void* hEGL = dlopen("libEGL.so", RTLD_NOW);
    if (!hEGL) { blog("ERROR dlopen libEGL"); return 0; }
    void* a = dlsym(hEGL, "eglSwapBuffers");
    if (!a) { blog("ERROR sym eglSwapBuffers"); return 0; }
    if (!gotHook(a, (void*)h_Swap, (void**)&o_Swap)) { blog("ERROR GOT hook gagal"); return 0; }
    inited = 1;
    blog("b3d_init OK (M1)");
    return 1;
}
EXPORT void b3d_enable(int on) { enabled = on; }
EXPORT int  b3d_frames(void) { return frames; }
