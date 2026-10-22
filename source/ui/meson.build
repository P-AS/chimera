# Chimera's Qt window (issue 142; a private prototype). Two parts:
#   viewmodel/  the state and actions behind each window, in plain C++ with no
#               Qt, over the engine's C interface. Scripts, the MCP server and
#               the tests drive these.
#   qt/         the Qt Quick window: QML for layout and bindings, a generic
#               adapter from any view model to QML, and the items that draw
#               the picture and the piano roll.

vm_inc = include_directories('viewmodel/include')
vm_lib = static_library(
  'chimera-vm',
  [
    'viewmodel/src/view_model.cpp',
    'viewmodel/src/bindings.cpp',
    'viewmodel/src/json.cpp',
    'viewmodel/src/session_vm.cpp',
    'viewmodel/src/piano_roll_vm.cpp',
    'viewmodel/src/gamepads.cpp',
    'viewmodel/src/sound.cpp',
    'viewmodel/src/lua_bridge.cpp',
    'viewmodel/src/ram_search_vm.cpp',
    'viewmodel/src/settings_vm.cpp',
  ],
  include_directories: [vm_inc, engine_inc, include_directories('../../extern/lua')],
  dependencies: [thread_dep, dl_dep],
  override_options: ['cpp_std=c++20'],
  pic: true,
)
vm_dep = declare_dependency(
  link_with: [vm_lib, chimera_shared, lua54],
  include_directories: [vm_inc, engine_inc],
  dependencies: [thread_dep],
)

test_vm = executable(
  'test_vm',
  'viewmodel/tests/test_vm.cpp',
  dependencies: [vm_dep],
  cpp_args: ['-UNDEBUG'],
  override_options: ['cpp_std=c++20'],
  build_by_default: true,
)
test(
  'view models',
  test_vm,
  args: [repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom',
    repo_root / 'tests/synth/movies', repo_root / 'tests/synth/goldens'],
  # OpenAL's device that plays to nowhere: the sound check runs with no speakers
  env: ['ALSOFT_DRIVERS=null'],
  timeout: 180,
)

qt6 = import('qt6')
qt_dep = dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'Quick', 'QuickControls2'], required: get_option('qt_ui'))
if qt_dep.found()
  qt_moc = qt6.compile_moc(
    headers: ['qt/qml_view_model.hpp', 'qt/picture_item.hpp', 'qt/piano_roll_item.hpp'],
    include_directories: [vm_inc, engine_inc],
    dependencies: [qt_dep],
  )
  # the resource compiler runs on the build machine; its Qt may pack with
  # zstd, which a Qt built without zstd (the Windows one) cannot unpack
  qt_rcc = qt6.compile_resources(sources: ['qt/ui.qrc'], extra_args: ['--no-zstd'])
  executable(
    'chimera-qt',
    [
      'qt/main.cpp',
      'qt/qml_view_model.cpp',
      'qt/picture_item.cpp',
      'qt/piano_roll_item.cpp',
      'qt/key_names.cpp',
      qt_moc,
      qt_rcc,
    ],
    dependencies: [vm_dep, qt_dep],
    # the view models have a Signal::emit; Qt's "emit" keyword would eat it
    cpp_args: ['-DQT_NO_KEYWORDS'],
    override_options: ['cpp_std=c++20'],
    install: true,
    install_dir: 'dll',
  )
endif
