# CMake generated Testfile for 
# Source directory: /home/danusan/Coding/ComponEx/engine
# Build directory: /home/danusan/Coding/ComponEx/build/engine
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_entity_manager "/home/danusan/Coding/ComponEx/build/engine/test_entity_manager")
set_tests_properties(test_entity_manager PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
add_test(test_component_registry "/home/danusan/Coding/ComponEx/build/engine/test_component_registry")
set_tests_properties(test_component_registry PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
add_test(test_archetype "/home/danusan/Coding/ComponEx/build/engine/test_archetype")
set_tests_properties(test_archetype PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
add_test(test_query "/home/danusan/Coding/ComponEx/build/engine/test_query")
set_tests_properties(test_query PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
add_test(test_collision "/home/danusan/Coding/ComponEx/build/engine/test_collision")
set_tests_properties(test_collision PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
add_test(test_serialization "/home/danusan/Coding/ComponEx/build/engine/test_serialization")
set_tests_properties(test_serialization PROPERTIES  _BACKTRACE_TRIPLES "/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;122;add_test;/home/danusan/Coding/ComponEx/engine/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
