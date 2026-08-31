// Replaced by SDLActivity/SDL_main path

typedef struct VerseEGLState {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
} VerseEGLState;

static VerseEGLState g_egl = {EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_CONTEXT};
static int g_surface_width = 0;
static int g_surface_height = 0;
static int g_rendering = 0;

// Simple GLES3 triangle program
static GLuint g_program = 0;
static GLuint g_vao = 0;
static GLuint g_vbo = 0;

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(s); return 0; }
    return s;
}

static int init_gl_pipeline(void)
{
    static const char *vs_src =
        "#version 300 es\n"
        "layout(location=0) in vec2 a_pos;\n"
        "void main(){ gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
    static const char *fs_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "out vec4 o_col;\n"
        "void main(){ o_col = vec4(1.0, 0.6, 0.1, 1.0); }\n";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) return 0;
    g_program = glCreateProgram();
    glAttachShader(g_program, vs);
    glAttachShader(g_program, fs);
    glLinkProgram(g_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(g_program, GL_LINK_STATUS, &linked);
    if (!linked) { glDeleteProgram(g_program); g_program = 0; return 0; }

    const GLfloat verts[] = {
        -0.6f, -0.6f,
         0.6f, -0.6f,
         0.0f,  0.6f,
    };
    glGenVertexArrays(1, &g_vao);
    glBindVertexArray(g_vao);
    glGenBuffers(1, &g_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void*)0);
    glBindVertexArray(0);
    return 1;
}

static void draw_frame(long frameTimeNanos, void *data)
{
    (void)frameTimeNanos; (void)data;
    if (!g_rendering || g_egl.display == EGL_NO_DISPLAY || g_egl.surface == EGL_NO_SURFACE)
        return;
    glViewport(0, 0, g_surface_width, g_surface_height);
    glClearColor(0.09f, 0.11f, 0.25f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_program);
    glBindVertexArray(g_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    eglSwapBuffers(g_egl.display, g_egl.surface);
    // Schedule next frame
    AChoreographer_postFrameCallback(AChoreographer_getInstance(), draw_frame, NULL);
}

static void verse_egl_destroy(void)
{
    if (g_egl.display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(g_egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_egl.context != EGL_NO_CONTEXT) eglDestroyContext(g_egl.display, g_egl.context);
        if (g_egl.surface != EGL_NO_SURFACE) eglDestroySurface(g_egl.display, g_egl.surface);
        eglTerminate(g_egl.display);
    }
    g_egl.display = EGL_NO_DISPLAY;
    g_egl.surface = EGL_NO_SURFACE;
    g_egl.context = EGL_NO_CONTEXT;
}

static int verse_egl_init(ANativeWindow *window)
{
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY)
        return 0;

    if (!eglInitialize(dpy, NULL, NULL))
        return 0;

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLConfig config = NULL;
    EGLint numConfig = 0;
    if (!eglChooseConfig(dpy, configAttribs, &config, 1, &numConfig) || numConfig < 1)
        return 0;

    EGLSurface surf = eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)window, NULL);
    if (surf == EGL_NO_SURFACE)
        return 0;

    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, ctxAttribs);
    if (ctx == EGL_NO_CONTEXT)
    {
        eglDestroySurface(dpy, surf);
        return 0;
    }

    if (!eglMakeCurrent(dpy, surf, surf, ctx))
    {
        eglDestroyContext(dpy, ctx);
        eglDestroySurface(dpy, surf);
        return 0;
    }

    g_egl.display = dpy;
    g_egl.surface = surf;
    g_egl.context = ctx;
    g_surface_width = ANativeWindow_getWidth(window);
    g_surface_height = ANativeWindow_getHeight(window);
    return 1;
}

static void on_native_window_created(ANativeActivity *activity, ANativeWindow *window)
{
    (void)activity;
    if (!window)
        return;
    if (verse_egl_init(window))
    {
        if (init_gl_pipeline())
        {
            g_rendering = 1;
            AChoreographer_postFrameCallback(AChoreographer_getInstance(), draw_frame, NULL);
            android_log(4, "Verse", "EGL + GL pipeline initialized, rendering started");
        }
        else
        {
            android_log(6, "Verse", "GL pipeline init failed");
        }
    }
    else
    {
        android_log(6, "Verse", "EGL init failed");
    }
}

static void on_native_window_destroyed(ANativeActivity *activity, ANativeWindow *window)
{
    (void)activity; (void)window;
    g_rendering = 0;
    verse_egl_destroy();
    android_log(4, "Verse", "EGL destroyed");
}

static void on_native_window_resized(ANativeActivity *activity, ANativeWindow *window)
{
    (void)activity;
    if (!window)
        return;
    g_surface_width = ANativeWindow_getWidth(window);
    g_surface_height = ANativeWindow_getHeight(window);
    android_log(4, "Verse", "Window resized to %dx%d", g_surface_width, g_surface_height);
}

static void on_native_window_redraw_needed(ANativeActivity *activity, ANativeWindow *window)
{
    (void)activity; (void)window;
    if (g_rendering)
        draw_frame(0, NULL);
}

static void on_resume(ANativeActivity *activity)
{
    (void)activity;
    g_rendering = 1;
    AChoreographer_postFrameCallback(AChoreographer_getInstance(), draw_frame, NULL);
    android_log(4, "Verse", "onResume: rendering resumed");
}

static void on_pause(ANativeActivity *activity)
{
    (void)activity;
    g_rendering = 0;
    android_log(4, "Verse", "onPause: rendering paused");
}

JNIEXPORT void ANativeActivity_onCreate(ANativeActivity *activity, void *savedState, size_t savedStateSize)
{
    (void)savedState;
    (void)savedStateSize;
    android_platform_init(activity->vm, activity->clazz);
    android_set_asset_manager(activity->assetManager);
    // Fullscreen + keep screen on
    ANativeActivity_setWindowFlags(activity, AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON,
                                   AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON);
    activity->callbacks->onNativeWindowCreated = on_native_window_created;
    activity->callbacks->onNativeWindowDestroyed = on_native_window_destroyed;
    activity->callbacks->onNativeWindowResized = on_native_window_resized;
    activity->callbacks->onNativeWindowRedrawNeeded = on_native_window_redraw_needed;
    activity->callbacks->onResume = on_resume;
    activity->callbacks->onPause = on_pause;
    android_log(4, "Verse", "ANativeActivity_onCreate: initialized platform + callbacks");
}


