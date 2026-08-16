/*
 * Copyright (c) 2012 Carsten Munk <carsten.munk@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <android-config.h>

#if ((ANDROID_VERSION_MAJOR >= 4 && ANDROID_VERSION_MINOR >= 2) || ANDROID_VERSION_MAJOR >= 5)

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
extern "C" {
#include <hybris/common/binding.h>
}

#include "test_common.h"

const char vertex_src [] =
"                                        \
   attribute vec4        position;       \
   varying mediump vec2  pos;            \
   uniform vec4          offset;         \
                                         \
   void main()                           \
   {                                     \
      gl_Position = position + offset;   \
      pos = position.xy;                 \
   }                                     \
";


const char fragment_src [] =
"                                                      \
   varying mediump vec2    pos;                        \
   uniform mediump float   phase;                      \
                                                       \
   void  main()                                        \
   {                                                   \
      gl_FragColor  =  vec4( 1., 0.9, 0.7, 1.0 ) *     \
        cos( 30.*sqrt(pos.x*pos.x + 1.5*pos.y*pos.y)   \
             + atan(pos.y,pos.x) - phase );            \
   }                                                   \
";


GLfloat norm_x    =  0.0;
GLfloat norm_y    =  0.0;
GLfloat offset_x  =  0.0;
GLfloat offset_y  =  0.0;
GLfloat p1_pos_x  =  0.0;
GLfloat p1_pos_y  =  0.0;

GLint phase_loc;
GLint offset_loc;
GLint position_loc;

const float vertexArray[] = {
	0.0,  1.0,  0.0,
	-1.,  0.0,  0.0,
	0.0, -1.0,  0.0,
	1.,  0.0,  0.0,
	0.0,  1.,  0.0
};

static void dump_tls(const char *stage)
{
	void **tls;
	__asm__("movl %%gs:0, %0" : "=r"(tls));
	printf("tls stage=%s base=%p", stage, tls);
	for (int i = 0; i < 12; ++i)
		printf(" slot%d=%p", i, tls[i]);
	printf("\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	EGLDisplay display;
	EGLConfig ecfg;
	EGLint num_config;
	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 32,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLSurface surface;
	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLContext context;

	EGLBoolean rv;

	HWComposer *win = create_hwcomposer_window();
	dump_tls("window");

	display = eglGetDisplay(NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(display != EGL_NO_DISPLAY);

	rv = eglInitialize(display, 0, 0);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);

	eglChooseConfig((EGLDisplay) display, attr, &ecfg, 1, &num_config);
	assert(eglGetError() == EGL_SUCCESS);
	assert(rv == EGL_TRUE);



	surface = eglCreateWindowSurface((EGLDisplay) display, ecfg, (EGLNativeWindowType) static_cast<ANativeWindow *> (win), NULL);
	assert(eglGetError() == EGL_SUCCESS);
	assert(surface != EGL_NO_SURFACE);

	context = eglCreateContext((EGLDisplay) display, ecfg, EGL_NO_CONTEXT, ctxattr);
	assert(eglGetError() == EGL_SUCCESS);
	assert(context != EGL_NO_CONTEXT);

	dump_tls("before-current");
	EGLBoolean make_current = eglMakeCurrent((EGLDisplay) display, surface, surface, context);
	EGLint make_current_error = eglGetError();
	printf("egl-make-current=%u error=0x%x requested-context=%p current-context=%p current-display=%p draw-surface=%p read-surface=%p\n",
	       make_current, make_current_error, context,
	       eglGetCurrentContext(), eglGetCurrentDisplay(),
	       eglGetCurrentSurface(EGL_DRAW), eglGetCurrentSurface(EGL_READ));
	assert(make_current == EGL_TRUE);
	assert(make_current_error == EGL_SUCCESS);
	dump_tls("after-current");
	for (unsigned int key = 0; key < 8; ++key) {
		unsigned int *specific = static_cast<unsigned int *>(pthread_getspecific(key));
		printf("glibc-specific key=%u ptr=%p", key, specific);
		if (specific) {
			for (int word = 0; word < 20; ++word)
				printf(" w%d=0x%x", word, specific[word]);
		}
		printf("\n");
	}
	FILE *maps = fopen("/proc/self/maps", "r");
	char map_line[512];
	uintptr_t gles_base = 0;
	uintptr_t img_base = 0;
	while (maps && fgets(map_line, sizeof(map_line), maps)) {
		unsigned long start = 0, offset = 1;
		if (strstr(map_line, "libGLESv2_POWERVR_ROGUE.so") &&
		    sscanf(map_line, "%lx-%*lx %*4s %lx", &start, &offset) == 2 &&
		    offset == 0) {
			gles_base = (uintptr_t)start;
		}
		if (strstr(map_line, "libIMGegl.so") &&
		    sscanf(map_line, "%lx-%*lx %*4s %lx", &start, &offset) == 2 &&
		    offset == 0)
			img_base = (uintptr_t)start;
	}
	if (maps)
		fclose(maps);
	void *img = android_dlopen("libIMGegl.so", RTLD_NOW);
	void *img_emutls = img ? android_dlsym(img, "__emutls_get_address") : NULL;
	void *native_emutls = dlsym(RTLD_DEFAULT, "__emutls_get_address");
	void *gles_emutls_got = gles_base ? *(void **)(gles_base + 0xeaf24) : NULL;
	void *img_getspecific_got = img_base ? *(void **)(img_base + 0x20ff8) : NULL;
	unsigned int *control = gles_base ? (unsigned int *)(gles_base + 0xeb4f0) : NULL;
	void *emutls_address = (img_emutls && control) ?
		((void *(*)(void *))img_emutls)(control) : NULL;
	printf("emutls-resolution gles-base=%p got=%p img=%p native=%p img-base=%p getspecific-got=%p control=%p size=%u align=%u index=%u init=%p address=%p value=%p\n",
	       (void *)gles_base, gles_emutls_got, img_emutls, native_emutls,
	       (void *)img_base, img_getspecific_got, control,
	       control ? control[0] : 0, control ? control[1] : 0,
	       control ? control[2] : 0, control ? (void *)control[3] : NULL,
	       emutls_address, emutls_address ? *(void **)emutls_address : NULL);
	void *vendor_context = context ? ((void **)context)[4] : NULL;
	void *gles_context = vendor_context ? ((void **)vendor_context)[8] : NULL;
	int set_current_result = 0;
	if (gles_base && gles_context)
		set_current_result = ((int (*)(void *))(gles_base + 0x3eb30))(gles_context);
	printf("p90-current-bridge wrapper=%p vendor-context=%p gles-context=%p setter-result=%d emutls-value=%p\n",
	       context, vendor_context, gles_context, set_current_result,
	       emutls_address ? *(void **)emutls_address : NULL);
	void **tls;
	__asm__("movl %%gs:0, %0" : "=r"(tls));
	void **hooks = static_cast<void **>(tls[8]);
	printf("hooks=%p glGetString-entry=%p\n", hooks, hooks ? hooks[0x408 / 4] : NULL);
	GLenum error_before = glGetError();
	const char *probe_version = (const char *)glGetString(GL_VERSION);
	GLenum error_after = glGetError();
	printf("gl-error-before=0x%x gl-version-pointer=%p gl-error-after=0x%x\n",
	       error_before, probe_version, error_after);
	if (probe_version)
		printf("gl-version=%s\n", probe_version);
	printf("gl-vendor=%s\ngl-renderer=%s\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER));
	glViewport(0, 0, 1080, 1920);
	glClearColor(0.08f, 0.20f, 0.85f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	EGLBoolean swap_result = eglSwapBuffers(display, surface);
	printf("p90-powerVR-frame swap=%u egl-error=0x%x gl-error=0x%x\n",
	       swap_result, eglGetError(), glGetError());
	fflush(stdout);
	sleep(8);
	return 0;

	const char *version = (const char *)glGetString(GL_VERSION);
	assert(version);
	printf("%s\n",version);

	GLuint shaderProgram = create_program(vertex_src, fragment_src);
	glUseProgram  ( shaderProgram );    // and select it for usage

	//// now get the locations (kind of handle) of the shaders variables
	position_loc  = glGetAttribLocation  ( shaderProgram , "position" );
	phase_loc     = glGetUniformLocation ( shaderProgram , "phase"    );
	offset_loc    = glGetUniformLocation ( shaderProgram , "offset"   );
	if ( position_loc < 0  ||  phase_loc < 0  ||  offset_loc < 0 ) {
		return 1;
	}

	//glViewport ( 0 , 0 , 800, 600); // commented out so it uses the initial window dimensions
	glClearColor ( 1. , 1. , 1. , 1.);    // background color
	float phase = 0;
	int i;
	for (i=0; i<1020*60; ++i) {
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f ( phase_loc , phase );  // write the value of phase to the shaders phase
		phase  =  fmodf ( phase + 0.5f , 2.f * 3.141f );    // and update the local variable

		glUniform4f ( offset_loc  ,  offset_x , offset_y , 0.0 , 0.0 );

		glVertexAttribPointer ( position_loc, 3, GL_FLOAT, GL_FALSE, 0, vertexArray );
		glEnableVertexAttribArray ( position_loc );
		glDrawArrays ( GL_TRIANGLE_STRIP, 0, 5 );

		eglSwapBuffers ( (EGLDisplay) display, surface );  // get the rendered buffer to the screen
	}

	printf("stop\n");

#if 0
	(*egldestroycontext)((EGLDisplay) display, context);
	printf("destroyed context\n");

	(*egldestroysurface)((EGLDisplay) display, surface);
	printf("destroyed surface\n");
	(*eglterminate)((EGLDisplay) display);
	printf("terminated\n");
	android_dlclose(baz);
#endif
	return 0;
}

#else
#include <stdio.h>

int main(int argc, char *argv[])
{
    printf("test_hwcomposer is not supported in this build\n");
    return 0;
}
#endif

// vim:ts=4:sw=4:noexpandtab
