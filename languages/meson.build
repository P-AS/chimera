# Chimera's language tables (docs/translations.md): one file per language,
# read at run time from beside the engine - in the build folder, and in the
# bundle's dll/languages. A language added is a file here and its code below.
foreach code : ['en', 'de', 'es', 'fr', 'it', 'ja', 'pt', 'zh_CN', 'ko', 'zh_TW', 'ru', 'uk', 'pl', 'cs', 'nl', 'sv', 'da', 'nb', 'fi', 'tr', 'hu', 'ro', 'el', 'id', 'vi', 'ca']
  configure_file(input: code + '.json', output: code + '.json', copy: true, install: true, install_dir: 'dll/languages')
endforeach
