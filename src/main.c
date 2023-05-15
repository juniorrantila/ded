#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <GL/glew.h>
#define GL_GLEXT_PROTOTYPES
#include <SDL2/SDL_opengl.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "./editor.h"
#include "./file_browser.h"
#include "./la.h"
#include "./free_glyph.h"
#include "./simple_renderer.h"
#include "./common.h"
#include "./lexer.h"
#include "./sv.h"

// TODO: Save file dialog
// Needed when ded is ran without any file so it does not know where to save.

// TODO: An ability to create a new file
// TODO: Delete a word
// TODO: Delete selection
// TODO: Undo/redo system

void MessageCallback(GLenum source,
                     GLenum type,
                     GLuint id,
                     GLenum severity,
                     GLsizei length,
                     const GLchar* message,
                     const void* userParam)
{
    (void) source;
    (void) id;
    (void) length;
    (void) userParam;
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}

static Free_Glyph_Atlas atlas = {0};
static Simple_Renderer sr = {0};
static Editor editor = {0};
static File_Browser fb = {0};

// TODO: display errors reported via flash_error right in the text editor window somehow
#define flash_error(...) do { fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

typedef struct {
    bool quit;
    bool is_fullscreen;
    SDL_Window* window;
} Handle_Events;
static void handle_events(Handle_Events*, Editor*, Simple_Renderer*);

int main(int argc, char **argv)
{
    Errno err;

    FT_Library library = {0};

    FT_Error error = FT_Init_FreeType(&library);
    if (error) {
        fprintf(stderr, "ERROR: Could not initialize FreeType2 library\n");
        return 1;
    }

    // TODO: users should be able to customize the font
    const char *const font_file_path = "./fonts/VictorMono-Regular.ttf";
    // const char *const font_file_path = "./fonts/iosevka-regular.ttf";

    FT_Face face;
    error = FT_New_Face(library, font_file_path, 0, &face);
    if (error == FT_Err_Unknown_File_Format) {
        fprintf(stderr, "ERROR: `%s` has an unknown format\n", font_file_path);
        return 1;
    } else if (error) {
        fprintf(stderr, "ERROR: Could not load file `%s`\n", font_file_path);
        return 1;
    }

    FT_UInt pixel_size = FREE_GLYPH_FONT_SIZE;
    error = FT_Set_Pixel_Sizes(face, 0, pixel_size);
    if (error) {
        fprintf(stderr, "ERROR: Could not set pixel size to %u\n", pixel_size);
        return 1;
    }

    if (argc > 1) {
        const char *file_path = argv[1];
        err = editor_load_from_file(&editor, file_path);
        if (err != 0) {
            fprintf(stderr, "ERROR: Could not read file %s: %s\n", file_path, strerror(err));
            return 1;
        }
    }

    const char *dir_path = ".";
    err = fb_open_dir(&fb, dir_path);
    if (err != 0) {
        fprintf(stderr, "ERROR: Could not read directory %s: %s\n", dir_path, strerror(err));
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "ERROR: Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window =
        SDL_CreateWindow("ded",
                         0, 0,
                         SCREEN_WIDTH, SCREEN_HEIGHT,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (window == NULL) {
        fprintf(stderr, "ERROR: Could not create SDL window: %s\n", SDL_GetError());
        return 1;
    }

    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

        int major;
        int minor;
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
        SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
        printf("GL version %d.%d\n", major, minor);
    }

    if (SDL_GL_CreateContext(window) == NULL) {
        fprintf(stderr, "ERROR: Could not create OpenGL context: %s\n", SDL_GetError());
        return 1;
    }

    GLenum gl_error = 0;
    if (GLEW_OK != (gl_error = glewInit()) && gl_error != GLEW_ERROR_NO_GLX_DISPLAY) {
        fprintf(stderr, "ERROR: Could not initialize GLEW! %d", gl_error);
        return 1;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (GLEW_ARB_debug_output) {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(MessageCallback, 0);
    } else {
        fprintf(stderr, "WARNING: GLEW_ARB_debug_output is not available");
    }

    simple_renderer_init(&sr);
    free_glyph_atlas_init(&atlas, face);

    editor.atlas = &atlas;
    editor_retokenize(&editor);

    Handle_Events context = (Handle_Events){
        .is_fullscreen = false,
        .quit = false,
        .window = window,
    };
    while (!context.quit) {
        const Uint32 start = SDL_GetTicks();
        handle_events(&context, &editor, &sr);

        Vec4f bg = hex_to_vec4f(0x181818FF);
        glClearColor(bg.x, bg.y, bg.z, bg.w);
        glClear(GL_COLOR_BUFFER_BIT);

        if (editor.mode == EDITOR_MODE_BROWSE) {
            fb_render(&fb, window, &atlas, &sr);
        } else {
            editor_render(&editor, window, &atlas, &sr);
        }

        SDL_GL_SwapWindow(window);

        const Uint32 duration = SDL_GetTicks() - start;
        const Uint32 delta_time_ms = 1000 / FPS;
        if (duration < delta_time_ms) {
            SDL_Delay(delta_time_ms - duration);
        }
    }

    return 0;
}

static void handle_events_normal_mode(Editor*, SDL_Event);
static void handle_events_insert_mode(Editor*, SDL_Event);
static void handle_events_browse_mode(Editor*, SDL_Event);
static_assert(__EDITOR_MODE_SIZE == 3, "Don't forget to update this");
static void (*event_handlers[__EDITOR_MODE_SIZE])(Editor*, SDL_Event) = {
    [EDITOR_MODE_NORMAL] = handle_events_normal_mode,
    [EDITOR_MODE_INSERT] = handle_events_insert_mode,
    [EDITOR_MODE_BROWSE] = handle_events_browse_mode,
};

static void handle_events(Handle_Events *context, Editor *editor, Simple_Renderer *sr)
{
    SDL_Event event = {0};
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT)
            context->quit = true;

        switch (event.type) {

        case SDL_WINDOWEVENT: {
            switch (event.window.event) {
            case SDL_WINDOWEVENT_RESTORED:
            case SDL_WINDOWEVENT_MAXIMIZED:
            case SDL_WINDOWEVENT_SIZE_CHANGED:
            case SDL_WINDOWEVENT_RESIZED: {
                int w = event.window.data1;
                int h = event.window.data2;
                glViewport(0, 0, w, h);
            }
            break;
            }
        }
        break;

        case SDL_KEYDOWN: {
            switch (event.key.keysym.sym) {
                case SDLK_F11: {
                    context->is_fullscreen = !context->is_fullscreen;
                    SDL_SetWindowFullscreen(context->window, context->is_fullscreen * SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                continue;

                case SDLK_F3: {
                    editor->mode = EDITOR_MODE_BROWSE;
                }
                continue;

                case SDLK_F5: {
                    simple_renderer_reload_shaders(sr);
                }
                continue;

                case SDLK_F2: {
                    if (editor->file_path.count > 0) {
                        Errno err = editor_save(editor);
                        if (err != 0) {
                            flash_error("Could not save currently edited file: %s", strerror(err));
                        }
                    } else {
                        // TODO: ask the user for the path to save to in this situation
                        flash_error("Nowhere to save the text");
                    }
                }
                continue;

            }
        } break;

        }

        event_handlers[editor->mode](editor, event);
    }
}

static void handle_events_normal_mode(Editor *editor, SDL_Event event)
{
    switch (event.type) {
    case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
        case SDLK_0:
        case SDLK_HOME: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_to_begin(editor);
            } else {
                editor_move_to_line_begin(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        } break;

        case SDLK_DOLLAR:
        case SDLK_END: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_to_end(editor);
            } else {
                editor_move_to_line_end(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        } break;


        case SDLK_ESCAPE: {
            if (!editor->selection && !editor->searching) {
                editor->mode = EDITOR_MODE_NORMAL;
            }
            editor_stop_search(editor);
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
        break;

        } break;

        case SDLK_o: {
            editor->mode = EDITOR_MODE_INSERT;
            SDL_FlushEvent(SDL_TEXTINPUT);
            if (event.key.keysym.mod & KMOD_SHIFT) {
                editor_move_line_up(editor);
            }
            editor_insert_char(editor, '\n');
        }

        case SDLK_i: {
            editor->mode = EDITOR_MODE_INSERT;
            SDL_FlushEvent(SDL_TEXTINPUT);
        } break;

        case SDLK_s: {
            if (editor->file_path.count > 0) {
                Errno err = editor_save(editor);
                if (err != 0) {
                    flash_error("Could not save currently edited file: %s", strerror(err));
                }
            } else {
                // TODO: ask the user for the path to save to in this situation
                flash_error("Nowhere to save the text");
            }
        }

        case SDLK_RETURN: {
            editor_move_line_down(editor);
        }
        break;

        case SDLK_x: {
            if (!editor->selection) {
                editor->selection = true;
                editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
                editor_move_char_right(editor);
                editor->last_stroke = SDL_GetTicks();
            }
            editor_clipboard_cut(editor);
        }
        break;

        case SDLK_DELETE: {
            editor_delete(editor, event.key.keysym.mod & KMOD_CTRL);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_a: {
            if (event.key.keysym.mod & KMOD_SHIFT) {
                editor_move_to_line_end(editor);
                editor->mode = EDITOR_MODE_INSERT;
                SDL_FlushEvent(SDL_TEXTINPUT);
            }
        }
        break;

        case SDLK_k:
        case SDLK_UP: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_paragraph_up(editor);
            } else {
                editor_move_line_up(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_j:
        case SDLK_DOWN: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_paragraph_down(editor);
            } else {
                editor_move_line_down(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_PAGEUP: {
            editor_move_page_up(editor);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_PAGEDOWN: {
            editor_move_page_down(editor);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_BACKSPACE:
        case SDLK_h:
        case SDLK_LEFT: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                case SDLK_b:
                editor_move_word_left(editor);
            } else {
                editor_move_char_left(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_l:
        case SDLK_RIGHT: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                case SDLK_w:
                editor_move_word_right(editor);
            } else {
                editor_move_char_right(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;
        }
    }
    break;
    }
}

static void handle_events_insert_mode(Editor *editor, SDL_Event event)
{
    switch (event.type) {
    case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE: {
            if (!editor->selection && !editor->searching) {
                editor->mode = EDITOR_MODE_NORMAL;
            }
            editor_stop_search(editor);
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
        }
        break;

        case SDLK_HOME: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_to_begin(editor);
            } else {
                editor_move_to_line_begin(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        } break;

        case SDLK_END: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_to_end(editor);
            } else {
                editor_move_to_line_end(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        } break;

        case SDLK_BACKSPACE: {
            editor_backspace(editor, event.key.keysym.mod & KMOD_CTRL);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_RETURN: {
            if (editor->searching) {
                editor_stop_search(editor);
            } else {
                editor_insert_char(editor, '\n');
                editor->last_stroke = SDL_GetTicks();
            }
        }
        break;

        case SDLK_DELETE: {
            editor_delete(editor, event.key.keysym.mod & KMOD_CTRL);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_f: {
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_start_search(editor);
            }
        }
        break;

        case SDLK_a: {
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor->selection = true;
                editor->select_begin = 0;
                editor->cursor = editor->data.count;
            }
        }
        break;

        case SDLK_TAB: {
            // TODO: indent on Tab instead of just inserting 4 spaces at the cursor
            // That is insert the spaces at the beginning of the line. Shift+TAB should
            // do unindent, that is remove 4 spaces from the beginning of the line.
            // TODO: customizable indentation style
            // - tabs/spaces
            // - tab width
            // - etc.
            for (size_t i = 0; i < 4; ++i) {
                editor_insert_char(editor, ' ');
            }
        }
        break;

        case SDLK_x: {
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_clipboard_cut(editor);
            }
        }
        break;

        case SDLK_c: {
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_clipboard_copy(editor);
            }
        }
        break;

        case SDLK_v: {
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_clipboard_paste(editor);
            }
        }
        break;

        case SDLK_UP: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_paragraph_up(editor);
            } else {
                editor_move_line_up(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_DOWN: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_paragraph_down(editor);
            } else {
                editor_move_line_down(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_PAGEUP: {
            editor_move_page_up(editor);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_PAGEDOWN: {
            editor_move_page_down(editor);
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_LEFT: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_word_left(editor);
            } else {
                editor_move_char_left(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;

        case SDLK_RIGHT: {
            editor_update_selection(editor, event.key.keysym.mod & KMOD_SHIFT);
            if (event.key.keysym.mod & KMOD_CTRL) {
                editor_move_word_right(editor);
            } else {
                editor_move_char_right(editor);
            }
            editor->last_stroke = SDL_GetTicks();
        }
        break;
    }
    break;
    }

    case SDL_TEXTINPUT: {
        const char *text = event.text.text;
        size_t text_len = strlen(text);
        for (size_t i = 0; i < text_len; ++i) {
            editor_insert_char(editor, text[i]);
        }
        editor->last_stroke = SDL_GetTicks();
    }
    break;
    }
}

static void handle_events_browse_mode(Editor *editor, SDL_Event event)
{
    switch (event.type) {
    case SDL_KEYDOWN: {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE: {
            editor->mode = EDITOR_MODE_NORMAL;
        }

        case SDLK_F3: {
            editor->mode = EDITOR_MODE_NORMAL;
        }
        break;

        case SDLK_k:
        case SDLK_UP: {
            if (fb.cursor > 0) fb.cursor -= 1;
        }
        break;

        case SDLK_j:
        case SDLK_DOWN: {
            if (fb.cursor + 1 < fb.files.count) fb.cursor += 1;
        }
        break;

        case SDLK_RETURN: {
            const char *file_path = fb_file_path(&fb);
            if (file_path) {
                File_Type ft;
                Errno err = type_of_file(file_path, &ft);
                if (err != 0) {
                    flash_error("Could not determine type of file %s: %s", file_path, strerror(err));
                } else {
                    switch (ft) {
                    case FT_DIRECTORY: {
                        err = fb_change_dir(&fb);
                        if (err != 0) {
                            flash_error("Could not change directory to %s: %s", file_path, strerror(err));
                        }
                    }
                    break;

                    case FT_REGULAR: {
                        // TODO: before opening a new file make sure you don't have unsaved changes
                        // And if you do, annoy the user about it. (just like all the other editors do)
                        err = editor_load_from_file(editor, file_path);
                        if (err != 0) {
                            flash_error("Could not open file %s: %s", file_path, strerror(err));
                        } else {
                            editor->mode = EDITOR_MODE_BROWSE;
                        }
                    }
                    break;

                    case FT_OTHER: {
                        flash_error("%s is neither a regular file nor a directory. We can't open it.", file_path);
                    }
                    break;

                    default:
                        UNREACHABLE("unknown File_Type");
                    }
                }
            }
        }
        break;
        }
    } break;
    }
}

// TODO: ability to search within file browser
// Very useful when you have a lot of files
// TODO: ability to remove trailing whitespaces
