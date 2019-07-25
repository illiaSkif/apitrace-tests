/*
 * Copyright © 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
/*
 * \file map_and_draw_coherent.cpp
 *
 * Test verifies correct work of coherent memory while tracing it with apitrace
 *
 * \author Andrii Kryvytskyi <andrii.o.kryvytskyi@globallogic.com>
 */


#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctime>

#include <glad/glad.h>
#include <GLFW/glfw3.h>


size_t getSystemPageSize() {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

static const GLchar* vert_shader_text =
    "#version 430 core\n"
    "#extension GL_ARB_uniform_buffer_object : require\n"
    "\n"
    "layout (location = 0) in vec3 pos;"
    "\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(pos, 1);\n"
    "}\n";

static const GLchar* frag_shader_text =
    "#version 430 core\n"
	"#extension GL_ARB_uniform_buffer_object : require\n"
	"\n"
    "layout(pixel_center_integer) in vec4 gl_FragCoord;\n"
    "\n"
    "layout(std430, binding = 1) buffer ssbo { vec4 color[65536]; };\n"
	"\n"
    "out vec4 fragColor;\n"
    "\n"
	"void main()\n"
	"{\n"
    "   fragColor = color[int(gl_FragCoord.x + 256 * gl_FragCoord.y)];\n"
    "}\n";

static GLuint prog;
static GLuint ssbo;
static GLuint fbo;
static GLuint rbo;
static GLuint vbo;
static GLuint vao;
static GLuint indices;
static void *coherent_memory[3];
static int indexes_count = 0;

void check_error(void) {
    GLenum error = glGetError();
    switch (error) {
    case GL_NO_ERROR:
        break;
    case GL_OUT_OF_MEMORY:
        exit(EXIT_SKIP);
    default:
        exit(EXIT_FAILURE);
    }
}

static void
reqire_extension(const char * extension, int gl_version = GLAD_GL_VERSION_4_4) {
    if (!gl_version &&
        !glfwExtensionSupported("extension")) {
        fprintf(stderr, "error: %s not supported\n", extension);
        glfwTerminate();
        exit(EXIT_SKIP);
    }
}

static void
build_programm(void) {

    // build and compile our shader program
    // ------------------------------------
    // vertex shader
    int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vert_shader_text, NULL);
    glCompileShader(vertexShader);
    // check for shader compile errors
    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        printf ("ERROR::SHADER::VERTEX::COMPILATION_FAILED %s\n", infoLog);
    }

    // fragment shader
    int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &frag_shader_text, NULL);
    glCompileShader(fragmentShader);
    // check for shader compile errors
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);

    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        printf ("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED %s\n", infoLog);
    }

    // link shaders
    prog = glCreateProgram();
    glAttachShader(prog, vertexShader);
    glAttachShader(prog, fragmentShader);
    glLinkProgram(prog);
    // check for linking errors
    glGetProgramiv(prog, GL_LINK_STATUS, &success);

    if (!success) {
        glGetProgramInfoLog(prog, 512, NULL, infoLog);
        printf ("ERROR::SHADER::PROGRAM::LINKING_FAILED %s\n", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    assert(prog);
}

static bool
setup_buffers(int width, int height) {
    reqire_extension("GL_ARB_uniform_buffer_object");
    reqire_extension("GL_ARB_buffer_storage");
    reqire_extension("GL_ARB_map_buffer_range");
    reqire_extension("GL_VMWX_map_buffer_debug");

    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);

    GLint size = width * height * 4 * sizeof(float);

    glBufferStorage(GL_SHADER_STORAGE_BUFFER, size, NULL,
            GL_MAP_WRITE_BIT |
            GL_MAP_PERSISTENT_BIT |
            GL_MAP_COHERENT_BIT |
            GL_DYNAMIC_STORAGE_BIT);

    coherent_memory[0] = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size,
                    GL_MAP_WRITE_BIT |
                    GL_MAP_PERSISTENT_BIT |
                    GL_MAP_COHERENT_BIT);

    assert(ssbo);

    /* Attach SSBO */
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    check_error();

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);

    glGenBuffers(1, &vbo);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferStorage(GL_ARRAY_BUFFER, size, NULL,
        GL_MAP_WRITE_BIT |
        GL_MAP_PERSISTENT_BIT |
        GL_MAP_COHERENT_BIT |
        GL_DYNAMIC_STORAGE_BIT);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    coherent_memory[1] = glMapBufferRange(GL_ARRAY_BUFFER, 0, size,
                    GL_MAP_WRITE_BIT |
                    GL_MAP_PERSISTENT_BIT |
                    GL_MAP_COHERENT_BIT);

    glGenBuffers(1, &indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
    glBufferStorage(GL_ELEMENT_ARRAY_BUFFER, size, NULL,
        GL_MAP_WRITE_BIT |
        GL_MAP_PERSISTENT_BIT |
        GL_MAP_COHERENT_BIT |
        GL_DYNAMIC_STORAGE_BIT);
    coherent_memory[2] = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, size,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        return false;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    check_error();

    return true;
}

void generate_colors(int width, int height, int step) {
    float* color = (float*)coherent_memory[0];
    int index = 0;

    for(int y = 0; y < height; y++)
    {
        for(int x = 0; x< width; x++)
        {
            color[index++] = (float) x / (float) width;
            color[index++] = (float) y / (float) height;
            color[index++] = (float) (x + y * width) / (float) (width * height);
            color[index++] = 1.0f;
        }
    }

    printf("Colors count %d \n", index / 4);
}

void generateVerticles(int width, int height, int step) {
    float* vertices = (float*)coherent_memory[1];
    unsigned int* indexes = (unsigned int*)coherent_memory[2];

    int index = 0;
    for (int y = (height / 2); y >= 0 - height / 2; y-=step)
    {
        for(int x = 0 - (width / 2); x <= width / 2; x+=step)
        {
            vertices[index++] = (float)x / (float)(width / 2);
            vertices[index++] = (float)y / (float)(height / 2);
            vertices[index++] = 0.0f;
        }
    }
    printf("Vertexes count %d \n", index / 3);

    int vertices_count = index / 3;
    index = 0;

    int cols = width / step;
    int rows = height / step;

    for (int y = 0; y < rows; y++)
    {
        indexes[index++] = (unsigned int)(y * (cols + 1));

        for (int x = 0; x <= cols; x++)
        {
            indexes[index++] = (unsigned int)(y * (cols + 1) + x);
            indexes[index++] = (unsigned int)((y + 1) * (cols + 1) + x);
        }

        indexes[index++] = (unsigned int)((y + 1) * (cols + 1) + (cols - 1));
    }
    indexes_count = index;

    printf("Indexes count %d \n", index);
}

void draw(GLFWwindow* window, int width, int height)
{
    GLsync fence;
    glUseProgram(prog);

    while (!glfwWindowShouldClose(window))
    {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, width, height);

        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT,
            GL_TIMEOUT_IGNORED);

        glBindVertexArray(vao);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices);
        glDrawElements(GL_TRIANGLE_STRIP, indexes_count, GL_UNSIGNED_INT, (void*)0);
        glBindVertexArray(0);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, 1024, 1024, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
}

// bool
// probe(int x, int y, int width) {

//     float* color = (float*)coherent_memory[0];

//     GLfloat pixels[4];
//     GLfloat expected[4];
//     expected[0] = color[x + y * width + 0];
//     expected[0] = color[x + y * width + 0];
//     expected[0] = color[x + y * width + 0];
//     expected[0] = color[x + y * width + 0];

//     glReadBuffer(GL_COLOR_ATTACHMENT0);
//     glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pixels);

//     int components = 4;
//     for (int p = 0; p < components; p++)
//         if (fabsf(pixels[p] - expected[p]) > 0.01)
//         {
//             return false;
//         }
//     return true;
// }

int main(int argc, char** argv) {
    bool pass = true;
    int width = 256;
    int height = 256;
    int x0 = width / 4;
    int x1 = width * 3 / 4;
    int y0 = height / 4;
    int y1 = height * 3 / 4;
    int i;
    int step = 8;

    printf("getSystemPageSize: %u\n",getSystemPageSize());
    glfwInit();

    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1024, 1024, "Map coherent", NULL, NULL);
    if (!window) {
        return EXIT_SKIP;
    }

    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
        return EXIT_FAILURE;
    }

    build_programm();

    if (!setup_buffers(width, height))
        return pass ? 0 : EXIT_FAILURE;

    generateVerticles(width, height, step);
    generate_colors(width, height, step);


    draw(window, width, height);

    // pass = probe(x0, y0, 0) && pass;
    // pass = probe(x1, y0, 1) && pass;
    // pass = probe(x0, y1, 2) && pass;
    // pass = probe(x1, y1, 3) && pass;

    glfwDestroyWindow(window);
    glfwTerminate();

    return pass ? 0 : EXIT_FAILURE;

}
