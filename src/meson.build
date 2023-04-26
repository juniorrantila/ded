
ded_exe = executable('ded', [
    'common.c',
    'editor.c',
    'file_browser.c',
    'free_glyph.c',
    'la.c',
    'lexer.c',
    'main.c',
    'simple_renderer.c',
  ], dependencies: [
    freetype2_dep,
    glew_dep,
    sdl2_dep,
  ], c_args: [
    '-Wno-declaration-after-statement',
    '-Wno-gnu-case-range',
  ])
