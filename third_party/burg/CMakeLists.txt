cmake_minimum_required(VERSION 3.12)

project(burg LANGUAGES C)

set(SRC_FILES
  be.c
  burs.c
  closure.c
  delta.c
  fe.c
  gram.tab.c
  item.c
  lex.c
  list.c
  main.c
  map.c
  nonterminal.c
  operator.c
  pattern.c
  plank.c
  queue.c
  rule.c
  string.c
  symtab.c
  table.c
  trim.c
  zalloc.c
)

add_executable(burg ${SRC_FILES})
if(MSVC)
  target_compile_definitions(burg PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
