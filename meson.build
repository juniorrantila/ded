project('ded', 'c',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'default_library=static',
    'buildtype=release',
  ])

common_options = [
  'warning_level=0',
  'default_library=static',
  'buildtype=release',
]
freetype2_proj = subproject('freetype2', default_options: [
  common_options,
  'brotli=disabled',
  'bzip2=disabled',
  'harfbuzz=disabled',
  'mmap=enabled',
  'png=disabled',
  'tests=disabled',
  'zlib=disabled'
])
freetype2_dep = freetype2_proj.get_variable('freetype_dep')

glew_proj = subproject('glew', default_options: [
  common_options,
  'egl=disabled',
  'glu=disabled',
])
glew_dep = glew_proj.get_variable('glew_dep')

if host_machine.system() == 'darwin'
  sdl2_dep = dependency('sdl2') # Easier to not compile from source on macos.
else
  sdl2_proj = subproject('sdl2', default_options: [
    common_options,
    'test=false',
    'run_test=false',
    'assertions=disabled',
    'use_threads=disabled',
    'use_atomic=disabled',
    'use_audio=disabled',
    'use_audio_alsa=disabled',
    'use_audio_pulseaudio=disabled',
    'use_audio_jack=disabled',
    'use_audio_pipewire=disabled',
    'use_cpuinfo=disabled',
    'use_events=enabled',
    'use_file=disabled',
    'use_joystick=disabled',
    'use_joystick_dinput=disabled',
    'use_joystick_xinput=disabled',
    'use_joystick_wgi=disabled',
    'use_joystick_virtual=disabled',
    'use_haptic=disabled',
    'use_hidapi=disabled',
    'use_sensor=disabled',
    'use_loadso=disabled',
    'use_locale=disabled',
    'use_render=enabled',
    'use_timers=enabled',
    'use_video=enabled',
    'use_video_x11=enabled',
    'use_video_wayland=disabled',
    'use_video_wayland_libdecor=disabled',
    'use_video_opengl=enabled',
    'use_video_openglesv2=disabled',
    'use_video_vulkan=disabled',
    'use_video_offscreen=disabled',
    'use_power=disabled',
    'use_filesystem=disabled',
    'use_dummies=false',
    'prefer_dlopen=false',
  ])
  sdl2_dep = sdl2_proj.get_variable('sdl2_dep')
endif

subdir('src')
