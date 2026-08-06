# One helper for the shape every ctest in this tree has.
#
# A test was four lines that never varied except in their arguments:
#
#   add_executable(foo_tests engine/tests/FooTests.cpp)
#   target_include_directories(foo_tests PRIVATE engine/include third_party)
#   target_link_libraries(foo_tests PRIVATE glm::glm EnTT::EnTT)
#   add_test(NAME foo COMMAND foo_tests)
#
# times 153, which is ~600 lines of build file saying the same thing. Worse than
# the length: the four lines are *separately* editable, so a target could be
# built and never registered, or registered under a name that does not match its
# executable. Both have happened in trees like this one; neither is possible
# when one call emits all four.
#
#   eng_add_test(foo
#     SOURCES engine/tests/FooTests.cpp
#     INCLUDES engine/include third_party
#     LIBS glm::glm EnTT::EnTT)
#
# The executable is `<name>_tests` and the ctest is `<name>`, which is the
# convention the tree already followed by hand -- and now cannot break.
#
# WORKING_DIRECTORY is passed through and otherwise left alone, which means
# ctest's own default: the build directory. Deliberately not "helpfully"
# defaulted to the source root -- tests here find assets through eng::assets and
# the RAVEN_ASSET_ROOT_DEV compile definition, not through the working
# directory, and quietly changing where 153 tests run is exactly the kind of
# refactor that turns green into red for a reason nobody can find.
#
# Deliberately thin. There is no dependency resolution, no automatic test
# grouping and no per-test property magic: the arguments below are the four
# things a test actually varies in, and anything a specific test needs beyond
# them is one plain CMake call after the fact, on the target this defines.
function(eng_add_test name)
  cmake_parse_arguments(T "" "WORKING_DIRECTORY" "SOURCES;INCLUDES;LIBS;DEFINES"
                        ${ARGN})
  if(NOT T_SOURCES)
    message(FATAL_ERROR "eng_add_test(${name}): SOURCES is required")
  endif()
  if(T_UNPARSED_ARGUMENTS)
    # Catches the copy-paste that drops a keyword -- silently building a test
    # against the wrong libraries is the failure this prevents.
    message(FATAL_ERROR
            "eng_add_test(${name}): unrecognised arguments: ${T_UNPARSED_ARGUMENTS}")
  endif()

  set(target ${name}_tests)
  add_executable(${target} ${T_SOURCES})
  if(T_INCLUDES)
    target_include_directories(${target} PRIVATE ${T_INCLUDES})
  endif()
  if(T_LIBS)
    target_link_libraries(${target} PRIVATE ${T_LIBS})
  endif()
  if(T_DEFINES)
    target_compile_definitions(${target} PRIVATE ${T_DEFINES})
  endif()

  if(T_WORKING_DIRECTORY)
    add_test(NAME ${name} COMMAND ${target}
             WORKING_DIRECTORY "${T_WORKING_DIRECTORY}")
  else()
    add_test(NAME ${name} COMMAND ${target})
  endif()
endfunction()
