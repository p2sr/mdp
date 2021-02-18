#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

char **config_read_cmd_whitelist(const char *path);
bool config_check_cmd_whitelist(char **cmds, const char *cmd);
void config_free_cmd_whitelist(char **cmds);

#endif
