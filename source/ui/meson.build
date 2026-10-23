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
    'viewmodel/src/input_log.cpp',
    'viewmodel/src/bindings.cpp',
    'viewmodel/src/json.cpp',
    'viewmodel/src/session_vm.cpp',
    'viewmodel/src/piano_roll_vm.cpp',
    'viewmodel/src/gamepads.cpp',
    'viewmodel/src/sound.cpp',
    'viewmodel/src/lua_bridge.cpp',
    'viewmodel/src/ram_search_vm.cpp',
    'viewmodel/src/settings_vm.cpp',
    'viewmodel/src/lua_console_vm.cpp',
    'viewmodel/src/branches_vm.cpp',
    'viewmodel/src/firmware.cpp',
    'viewmodel/src/suggestion.cpp',
    'viewmodel/src/european_clock.cpp',
    'viewmodel/src/project_details_vm.cpp',
    'viewmodel/src/cores_vm.cpp',
    'viewmodel/src/branch_state.cpp',
    'viewmodel/src/watch_values.cpp',
    'viewmodel/src/settings_form.cpp',
    'viewmodel/src/new_project_vm.cpp',
    'viewmodel/src/hex_editor_vm.cpp',
    'viewmodel/src/controls_vm.cpp',
    'viewmodel/src/hotkeys_vm.cpp',
    'viewmodel/src/themes_vm.cpp',
    'viewmodel/src/ram_watch_vm.cpp',
    'viewmodel/src/config_store.cpp',
    'viewmodel/src/help_vm.cpp',
    'viewmodel/src/batch_run_vm.cpp',
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
# a second core that takes the synthetic core's files, for choosing between them
synth_again = custom_target(
  'synth-again',
  output: 'synth-again.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Again', '@OUTPUT@'],
  build_always_stale: true,
  build_by_default: true,
)
# a package of two machines with declared slots, for New Project
synth_slots = custom_target(
  'synth-slots',
  output: 'synth-slots.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/slots-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', '@OUTPUT@'],
  build_always_stale: true,
  build_by_default: true,
)
# a firmware that declares the hash of its original file, in a core that is
# not a game and in one that is
logo_firmware = '[{"id": "logo", "display": "The logo", "name": "logo.bin", "sha1": "CE31CFD83A86A850B5B110A3AC05B634F186C1F1", "required": true}]'
synth_hashed = custom_target(
  'synth-hashed',
  output: 'synth-hashed.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Hashed', '@OUTPUT@', '', logo_firmware],
  build_always_stale: true,
  build_by_default: true,
)
# a controller with a second player, keyboard keys and a console button
more_buttons = '["P2 A", "P2 B", "Power", "Key A", "Key B", "Key C", "Key D", "Key E", "Key F", "Key G", "Key H", "Key I", "Key J", "Key K", "Key L", "Key M", "Key N", "Key O", "Key P", "Key Q", "Key R", "Key S", "Key T", "Key U", "Key V", "Key W", "Key X", "Key Y", "Key Z", "Key 0", "Key 1", "Key 2", "Key 3", "Key 4", "Key 5", "Key 6", "Key 7", "Key 8", "Key 9"]'
synth_keys = custom_target(
  'synth-keys',
  output: 'synth-keys.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Keys', '@OUTPUT@', '', '', '', more_buttons],
  build_always_stale: true,
  build_by_default: true,
)
# a game core that needs no firmware, for the Game menu
synth_plain_game = custom_target(
  'synth-plain-game',
  output: 'synth-plain-game.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Plain Game', '@OUTPUT@', 'game'],
  build_always_stale: true,
  build_by_default: true,
)
synth_game = custom_target(
  'synth-game',
  output: 'synth-game.chimeraCore',
  command: [find_program('python3'), files('viewmodel/tests/renamed-package.py'),
    repo_root / 'build/Cores/synth-box.chimeraCore', 'Synth Game', '@OUTPUT@', 'game', logo_firmware,
    '2026-10-18T16:35:00Z'],
  build_always_stale: true,
  build_by_default: true,
)
test(
  'view models',
  test_vm,
  args: [repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom',
    repo_root / 'tests/synth/movies', repo_root / 'tests/synth/goldens', synth_again, synth_slots, synth_hashed, synth_game, synth_keys, synth_plain_game],
  # OpenAL's device that plays to nowhere: the sound check runs with no speakers
  env: ['ALSOFT_DRIVERS=null'],
  timeout: 180,
)

qt6 = import('qt6')
qt_dep = dependency('qt6', modules: ['Core', 'Gui', 'Qml', 'Quick', 'QuickControls2'], required: get_option('qt_ui'))
if qt_dep.found()
  qt_moc = qt6.compile_moc(
    headers: ['qt/qml_view_model.hpp', 'qt/picture_item.hpp', 'qt/piano_roll_item.hpp', 'qt/key_names.hpp', 'qt/theme_palette.hpp', 'qt/clipboard_relay.hpp', 'qt/picture_files.hpp'],
    include_directories: [vm_inc, engine_inc],
    dependencies: [qt_dep],
  )
  # the resource compiler runs on the build machine; its Qt may pack with
  # zstd, which a Qt built without zstd (the Windows one) cannot unpack
  qt_rcc = qt6.compile_resources(sources: ['qt/ui.qrc'], extra_args: ['--no-zstd'])
  chimera_qt = executable(
    'chimera-qt',
    [
      'qt/main.cpp',
      'qt/qml_view_model.cpp',
      'qt/picture_item.cpp',
      'qt/piano_roll_item.cpp',
      'qt/key_names.cpp',
      'qt/theme_palette.cpp',
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
  # the whole window loads (every QML file, every view model bound), runs a
  # game for a second and quits cleanly: a QML mistake fails here, not at
  # someone's desk
  test(
    'window loads',
    chimera_qt,
    args: [repo_root / 'build/Cores/synth-box.chimeraCore', repo_root / 'tests/synth/roms/gridWalker.testrom',
      '--play', '--measure', '1', '--show', 'lua'],
    env: ['QT_QPA_PLATFORM=offscreen', 'QT_QUICK_BACKEND=software', 'ALSOFT_DRIVERS=null',
      'CHIMERA_QT_CONFIG=' + meson.current_build_dir() / 'window-test-settings.json',
      'CHIMERA_QT_CACHE=' + meson.current_build_dir() / 'window-test-cache'],
    timeout: 60,
  )
endif
