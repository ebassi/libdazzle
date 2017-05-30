util_headers = [
  'dzl-cairo.h',
  'dzl-counter.h',
  'dzl-date-time.h',
  'dzl-dnd.h',
  'dzl-file-manager.h',
  'dzl-heap.h',
  'dzl-pango.h',
  'dzl-rgba.h',
]

util_sources = [
  'dzl-cairo.c',
  'dzl-counter.c',
  'dzl-date-time.c',
  'dzl-dnd.c',
  'dzl-file-manager.c',
  'dzl-heap.c',
  'dzl-pango.c',
  'dzl-rgba.c',
  'dzl-util.c',
]

libdazzle_public_headers += files(util_headers)
libdazzle_public_sources += files(util_sources)

install_headers(util_headers, subdir: join_paths(libdazzle_header_subdir, 'util'))
