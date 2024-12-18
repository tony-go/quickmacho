extern "C" {
#include "../lib/libmachore.h"
}

#include <gtest/gtest.h>

#include <stdio.h>
#include <stdlib.h>

static void read_file_to_buffer(const char *filename, uint8_t **buffer,
                                size_t *size) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    printf("Error: Cannot open file '%s'\n", filename);
    return;
  }

  fseek(file, 0, SEEK_END);
  *size = ftell(file);
  fseek(file, 0, SEEK_SET);

  *buffer = (uint8_t *)malloc(*size);
  if (!*buffer) {
    printf("Error: Memory allocation failed\n");
    fclose(file);
    return;
  }

  if (fread(*buffer, 1, *size, file) != *size) {
    printf("Error: Failed to read file\n");
    free(*buffer);
    fclose(file);
    return;
  }
  fclose(file);
}

TEST(libmachore, create_analysis) {
  struct analysis analysis;
  create_analysis(&analysis);

  EXPECT_EQ(analysis.arch_analyses, nullptr);
  EXPECT_EQ(analysis.num_arch_analyses, 0);
  EXPECT_EQ(analysis.is_fat, false);

  clean_analysis(&analysis);
}

TEST(libmachore, clean_analysis) {
  struct analysis analysis;
  create_analysis(&analysis);
  analysis.arch_analyses =
      (struct arch_analysis *)malloc(sizeof(struct arch_analysis));
  analysis.num_arch_analyses = 1;

  clean_analysis(&analysis);

  EXPECT_EQ(analysis.arch_analyses, nullptr);
  EXPECT_EQ(analysis.num_arch_analyses, 0);
  EXPECT_EQ(analysis.is_fat, false);
}

TEST(libmachore, parse_macho) {
  struct analysis analysis;
  create_analysis(&analysis);

  const char *filename = "/bin/ls";
  uint8_t *buffer = nullptr;
  size_t buffer_size = 0;
  read_file_to_buffer(filename, &buffer, &buffer_size);

  parse_macho(&analysis, buffer, buffer_size);

  EXPECT_EQ(analysis.num_arch_analyses, 2);
  EXPECT_EQ(analysis.is_fat, true);

  free(buffer);
  clean_analysis(&analysis);
}

TEST(libmachore, parse_macho_arch) {
  struct analysis analysis;
  create_analysis(&analysis);

  const char *filename = "/bin/ls";
  uint8_t *buffer = nullptr;
  size_t buffer_size = 0;
  read_file_to_buffer(filename, &buffer, &buffer_size);

  parse_macho(&analysis, buffer, buffer_size);
  struct arch_analysis *arch_analysis = &analysis.arch_analyses[0];

  EXPECT_STREQ(arch_analysis->architecture, "x86_64");
  EXPECT_EQ(arch_analysis->filetype, FILETYPE_EXEC);
  EXPECT_EQ(arch_analysis->num_dylibs, 3);

  free(buffer);
  clean_analysis(&analysis);
}

TEST(libmachore, parse_macho_dylib) {
  struct analysis analysis;
  create_analysis(&analysis);

  const char *filename = "/bin/ls";
  uint8_t *buffer = nullptr;
  size_t buffer_size = 0;
  read_file_to_buffer(filename, &buffer, &buffer_size);

  parse_macho(&analysis, buffer, buffer_size);

  struct arch_analysis *arch_analysis = &analysis.arch_analyses[0];
  struct dylib_info *dylib_info_1 = &arch_analysis->dylibs[0];
  EXPECT_STREQ(dylib_info_1->path, "/usr/lib/libutil.dylib");
  EXPECT_FALSE(dylib_info_1->version[0] == '\0');

  struct dylib_info *dylib_info_2 = &arch_analysis->dylibs[1];
  EXPECT_STREQ(dylib_info_2->path, "/usr/lib/libncurses.5.4.dylib");
  EXPECT_FALSE(dylib_info_2->version[0] == '\0');

  struct dylib_info *dylib_info_3 = &arch_analysis->dylibs[2];
  EXPECT_STREQ(dylib_info_3->path, "/usr/lib/libSystem.B.dylib");
  EXPECT_FALSE(dylib_info_3->version[0] == '\0');

  free(buffer);
  clean_analysis(&analysis);
}

TEST(libmachore, parse_macho_strings) {
  struct analysis analysis;
  create_analysis(&analysis);

  const char *filename = "/bin/ls";
  uint8_t *buffer = nullptr;
  size_t buffer_size = 0;
  read_file_to_buffer(filename, &buffer, &buffer_size);

  parse_macho(&analysis, buffer, buffer_size);

  struct arch_analysis *arch_analysis = &analysis.arch_analyses[0];
  struct string_info *string_info = &arch_analysis->strings[0];
  EXPECT_STREQ(string_info->content, "bin/ls");
  EXPECT_EQ(string_info->size, 7);
  EXPECT_STREQ(string_info->original_segment, "__TEXT");
  EXPECT_STREQ(string_info->original_section, "__cstring");
  EXPECT_EQ(string_info->original_offset, 0x00004A34);

  struct string_info *string_info_1 = &arch_analysis->strings[1];
  EXPECT_STREQ(string_info_1->content, "Unix2003");
  EXPECT_EQ(string_info_1->size, 9);
  EXPECT_STREQ(string_info->original_segment, "__TEXT");
  EXPECT_STREQ(string_info_1->original_section, "__cstring");
  EXPECT_EQ(string_info_1->original_offset, 0x00004A3B);

  free(buffer);
  clean_analysis(&analysis);
}
