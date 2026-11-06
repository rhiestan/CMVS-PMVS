ADD_EXECUTABLE(tex_kernel_test tex_kernel_test.cc)
TARGET_INCLUDE_DIRECTORIES(tex_kernel_test PRIVATE ${CMAKE_SOURCE_DIR}/base/pmvs)
TARGET_LINK_LIBRARIES(tex_kernel_test pmvs_lib)
ADD_TEST(NAME tex_kernel_test COMMAND tex_kernel_test)
