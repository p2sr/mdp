#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

char **config_read_newline_sep(const char *path);
bool config_check_cmd_whitelist(char **cmds, const char *cmd);
bool config_check_sum_whitelist(char **sums, uint32_t sum);
void config_free_newline_sep(char **paths);

#endif
