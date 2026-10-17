# Expat is compiled in its own target: its C sources include <memory.h>, which
# would collide with lib/Memory/Memory.h on case-insensitive file systems if
# lib/Memory were on the same include path.
add_library(txt_test_expat STATIC
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)
target_include_directories(txt_test_expat PUBLIC ${REPO_ROOT}/lib/expat)
target_compile_definitions(txt_test_expat PUBLIC
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

add_executable(TxtToHtmlTest
  TxtToHtmlTest.cpp
  ${REPO_ROOT}/lib/Txt/TxtToHtml.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(TxtToHtmlTest PRIVATE
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Txt
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
)

target_link_libraries(TxtToHtmlTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
  txt_test_expat
)

gtest_discover_tests(TxtToHtmlTest)
