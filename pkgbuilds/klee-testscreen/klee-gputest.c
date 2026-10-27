/* klee-gputest: does the Mali answer through libhybris? Prints GL_RENDERER (exit 0) */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
int main(void)
{
	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	EGLint maj, min, n;
	if (!eglInitialize(d, &maj, &min)) { printf("eglInitialize failed 0x%x\n", eglGetError()); return 1; }
	printf("EGL %d.%d vendor %s\n", maj, min, eglQueryString(d, EGL_VENDOR));
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig c;
	eglChooseConfig(d, ca, &c, 1, &n);
	EGLint sa[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(d, c, sa);
	EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
	if (!eglMakeCurrent(d, s, s, x)) { printf("eglMakeCurrent failed 0x%x\n", eglGetError()); return 1; }
	printf("GL_RENDERER %s\nGL_VERSION %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	eglTerminate(d);
	return 0;
}
