#ifndef _CORE_CONFIG_H_
#define _CORE_CONFIG_H_

typedef struct {
	char *botname;	/* Name of the bot as seen by user. */
	char *userfile;	/* File we store users in. */

	/* Telnet stuff. */
	char *telnet_vhost;
	int telnet_port;
	int telnet_stealth;
	int telnet_max_retries;
} core_config_t;

extern core_config_t core_config;

void core_config_init();
void core_config_save();

#endif
