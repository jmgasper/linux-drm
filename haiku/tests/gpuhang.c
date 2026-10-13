/* Makes the graphics ring time out on purpose: a fragment shader that loops
   for minutes over a small pbuffer, so amdgpu's job timeout (10 s) and its
   GPU recovery run. For finding out what a GPU hang does to the machine.

   gpuhang [iterations]   (default 2^31) */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char* kVertex =
	"attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }";
static const char* kFragment =
	"precision highp float; precision highp int; uniform highp int n; uniform float seed;\n"
	"void main() { float a = seed; for (highp int i = 0; i < 2147483647; i++) {\n"
	"  if (i >= n) break; a = fract(a * 1.37 + 0.11); }\n"
	"  gl_FragColor = vec4(a, 0.0, 0.0, 1.0); }";

static GLuint shader(GLenum type, const char* text)
{
	GLuint s = glCreateShader(type);
	glShaderSource(s, 1, &text, NULL);
	glCompileShader(s);
	GLint ok;
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, NULL, log); fprintf(stderr, "%s\n", log); exit(1); }
	return s;
}

int main(int argc, char** argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 2147483647;
	EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	EGLConfig c; EGLint count;
	eglChooseConfig(d, ca, &c, 1, &count);
	EGLint sa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(d, c, sa);
	EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext x = eglCreateContext(d, c, EGL_NO_CONTEXT, xa);
	eglMakeCurrent(d, s, s, x);
	printf("renderer: %s\n", glGetString(GL_RENDERER));
	GLuint p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER, kVertex));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER, kFragment));
	glBindAttribLocation(p, 0, "p");
	glLinkProgram(p);
	glUseProgram(p);
	glUniform1i(glGetUniformLocation(p, "n"), n);
	glUniform1f(glGetUniformLocation(p, "seed"), 0.5f);
	static const float quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	time_t start = time(NULL);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char pixel[4];
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
	printf("returned after %ld s, pixel %u, error 0x%x, reset status 0x%x\n",
		(long)(time(NULL) - start), pixel[0], glGetError(), 0);
	return 0;
}
