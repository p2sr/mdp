#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>

char **config_read_newline_sep(const char *path);
bool config_check_cmd_whitelist(char **cmds, const char *cmd);
bool config_check_sum_whitelist(char **sums, uint32_t sum);
void config_free_newline_sep(char **paths);

struct var_whitelist {
	char *var_name;
	char *val;
};

struct var_whitelist *config_read_var_whitelist(const char *path);
// 0: not present, 1: present but not matching, 2: matching
int config_check_var_whitelist(struct var_whitelist *list, const char *var, const char *val);
void config_free_var_whitelist(struct var_whitelist *list);

#endif
