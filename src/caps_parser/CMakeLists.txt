project(caps-parser)

add_executable(
  caps-parser
  test_parser.c caps_parser.c "${CMAKE_SOURCE_DIR}/src/mfm_utils/mfm_utils.c"
  "${CMAKE_SOURCE_DIR}/src/write_flux_opts.c")
