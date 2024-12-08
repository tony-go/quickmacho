#include <mach-o/dyld.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libmachore.h"

bool is_fat_header(uint8_t *buffer)
{
  uint32_t magic = *(uint32_t *)buffer;
  return magic == FAT_MAGIC || magic == FAT_CIGAM ||
         magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64;
}

void create_analysis(struct analysis *analysis)
{
  analysis->arch_analyses = NULL;
  analysis->num_arch_analyses = 0;
  analysis->is_fat = false;
}

void init_arch_analysis(struct arch_analysis *arch_analysis)
{
  arch_analysis->dylibs = NULL;
  arch_analysis->num_dylibs = 0;
}

void clean_arch_analysis(struct arch_analysis *arch_analysis)
{
  free(arch_analysis->dylibs);
  arch_analysis->num_dylibs = 0;
}

void clean_analysis(struct analysis *analysis)
{
  for (size_t i = 0; i < analysis->num_arch_analyses; i++)
  {
    clean_arch_analysis(&analysis->arch_analyses[i]);
  }
  free(analysis->arch_analyses);
  analysis->num_arch_analyses = 0;
}

// The version is a 32-bit integer in the format 0xMMmmPPPP, where MM is the
// major version, mm is the minor version, and PPPP is the patch version.
// We need to extract these values and print them in the format MM.mm.PPPP.
bool parse_dylib_version(struct dylib_command *cmd, char *output_version_str, size_t output_version_str_size)
{
  uint32_t version = cmd->dylib.current_version;
  uint32_t major = (version >> 24) & 0xFF;
  uint32_t minor = (version >> 16) & 0xFF;
  uint32_t patch = version & 0xFF;
  char *version_str = malloc(10);
  // return a string in the format MM.mm.PPPP
  size_t written = snprintf(output_version_str, output_version_str_size, "%u.%u.%u", major,
                            minor, patch);
  return written >= output_version_str_size;
}

bool parse_dylib_name(struct dylib_command *cmd, char *output_name_str, size_t output_name_str_size)
{
  const char *name = (char *)cmd + cmd->dylib.name.offset;
  size_t written = snprintf(output_name_str, output_name_str_size, "%s", name);
  return written >= output_name_str_size;
}

void parse_load_commands(struct arch_analysis *arch_analysis, uint8_t *buffer, uint32_t ncmds)
{
  struct mach_header *header = (struct mach_header *)buffer;
  uint32_t magic_header = header->magic;

  uint8_t *cmd;
  if (magic_header == MH_MAGIC_64)
  {
    cmd = buffer + sizeof(struct mach_header_64);
  }
  else
  {
    cmd = buffer + sizeof(struct mach_header);
  }

  for (uint32_t index = 0; index < ncmds; index++)
  {
    struct load_command *lc = (struct load_command *)cmd;

    switch (lc->cmd)
    {
    case LC_LOAD_DYLIB:
    case LC_LOAD_WEAK_DYLIB:
    case LC_ID_DYLIB:
    case LC_REEXPORT_DYLIB:
    case LC_LOAD_UPWARD_DYLIB:
    case LC_LAZY_LOAD_DYLIB:
    {
      struct dylib_command *dylib_cmd = (struct dylib_command *)cmd;

      arch_analysis->num_dylibs++;
      arch_analysis->dylibs = realloc(arch_analysis->dylibs,
                                      arch_analysis->num_dylibs * sizeof(struct dylib_info));

      struct dylib_info *dylib_info = &arch_analysis->dylibs[arch_analysis->num_dylibs - 1];

      char name_str[LIBMACHORE_DYLIB_PATH_SIZE];
      bool is_name_truncated = parse_dylib_name(dylib_cmd, name_str, LIBMACHORE_DYLIB_PATH_SIZE);
      dylib_info->is_path_truncated = is_name_truncated;
      strncpy(dylib_info->path, name_str, LIBMACHORE_DYLIB_PATH_SIZE);

      char version_str[LIBMACHORE_DYLIB_VERSION_SIZE];
      parse_dylib_version(dylib_cmd, version_str, LIBMACHORE_DYLIB_VERSION_SIZE);
      strncpy(dylib_info->version, version_str, LIBMACHORE_DYLIB_VERSION_SIZE);
      break;
    }
    default:
      break;
    }

    cmd += lc->cmdsize;
  }
}

void parse_mach_o(struct analysis *analysis, int arch_index, uint8_t *buffer)
{
  // TODO(tonygo):
  // - Check if the realloc failed
  // - Offload this allocation to a function
  analysis->num_arch_analyses++;
  analysis->arch_analyses = realloc(analysis->arch_analyses,
                                    analysis->num_arch_analyses * sizeof(struct arch_analysis));

  struct arch_analysis *arch_analysis = &analysis->arch_analyses[arch_index];

  struct mach_header *header = (struct mach_header *)buffer;
  uint32_t cpu_type = header->cputype;

  switch (cpu_type)
  {
  case CPU_TYPE_X86:
    strncpy(arch_analysis->architecture, "x86", LIBMACHORE_ARCHITECTURE_SIZE);
    break;
  case CPU_TYPE_X86_64:
    strncpy(arch_analysis->architecture, "x86_64", LIBMACHORE_ARCHITECTURE_SIZE);
    break;
  case CPU_TYPE_ARM:
    strncpy(arch_analysis->architecture, "ARM", LIBMACHORE_ARCHITECTURE_SIZE);
    break;
  case CPU_TYPE_ARM64:
    strncpy(arch_analysis->architecture, "ARM64", LIBMACHORE_ARCHITECTURE_SIZE);
    break;
  default:
    strncpy(arch_analysis->architecture, "Unknown", LIBMACHORE_ARCHITECTURE_SIZE);
  }

  uint32_t filetype = header->filetype;
  switch (filetype)
  {
  case MH_DYLIB:
    arch_analysis->filetype = FILETYPE_DYLIB;
    break;
  case MH_EXECUTE:
    arch_analysis->filetype = FILETYPE_EXEC;
    break;
  case MH_BUNDLE:
    arch_analysis->filetype = FILETYPE_BUNDLE;
    break;
  case MH_OBJECT:
    arch_analysis->filetype = FILETYPE_OBJECT;
    break;
  default:
    arch_analysis->filetype = FILETYPE_NOT_SUPPORTED;
    break;
  }

  uint32_t ncmds = header->ncmds;
  parse_load_commands(arch_analysis, buffer, ncmds);
}

void parse_macho(struct analysis *analysis, uint8_t *buffer, size_t size)
{
  bool is_fat = is_fat_header(buffer);
  if (is_fat)
  {
    analysis->is_fat = true;
    struct fat_header *header = (struct fat_header *)buffer;
    uint32_t nfat_arch = ntohl(header->nfat_arch);

    for (uint32_t arch_index = 0; arch_index < nfat_arch; arch_index++)
    {
      struct fat_arch *arch =
          (struct fat_arch *)(buffer + sizeof(struct fat_header) +
                              arch_index * sizeof(struct fat_arch));
      uint32_t offset = ntohl(arch->offset);
      parse_mach_o(analysis, arch_index, buffer + offset);
    }
  }
  else
  {
    parse_mach_o(analysis, 0, buffer);
  }
}
