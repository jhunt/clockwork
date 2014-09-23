/*
  Copyright 2011-2014 James Hunt <james@jameshunt.us>

  This file is part of Clockwork.

  Clockwork is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Clockwork is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Clockwork.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <termios.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sodium.h>

#include "../src/clockwork.h"
#include "../src/mesh.h"
#include "../src/gear/gear.h"

#define PROMPT_ECHO   1
#define PROMPT_NOECHO 0

static int SHOW_OPTOUTS = 0;

static char* s_prompt(const char *prompt, int echo)
{
	struct termios term_new, term_old;
	if (!echo) {
		if (tcgetattr(0, &term_old) != 0)
			return NULL;
		term_new = term_old;
		term_new.c_lflag &= ~ECHO;

		if (tcsetattr(0, TCSAFLUSH, &term_new) != 0)
			return NULL;
	}

	int attempts = 0;
	char answer[8192] = {0};
	while (!*answer) {
		if (attempts++ > 1) fprintf(stderr, "\n");
		fprintf(stderr, "%s", prompt);
		if (!fgets(answer, 8192, stdin))
			answer[0] = '\0';

		char *newline = strchr(answer, '\n');
		if (newline) *newline = '\0';
	}

	if (!echo) {
		tcsetattr(0, TCSAFLUSH, &term_old);
		fprintf(stderr, "\n");
	}

	return strdup(answer);
}

typedef struct {
	char      *endpoint;
	char      *username;
	char      *password;
	char      *authkey;
	cmd_t     *command;
	char      *filters;

	int        timeout_ms;
	int        sleep_ms;

	cw_cert_t *client_cert;
	cw_cert_t *master_cert;
	cw_cert_t *auth_cert;
} client_t;

static client_t* s_client_new(void)
{
	client_t *c = cw_alloc(sizeof(client_t));

	c->client_cert = cw_cert_generate(CW_CERT_TYPE_ENCRYPTION);

	c->username = getenv("USER");
	if (c->username) c->username = strdup(c->username);

	if (getenv("HOME")) {
		c->authkey = cw_string("%s/.clockwork/mesh.key", getenv("HOME"));

		struct stat st;
		if (stat(c->authkey, &st) != 0) {
			cw_log(LOG_INFO, "Default key %s not found; skipping implicit keyauth", c->authkey);
			free(c->authkey);
			c->authkey = NULL;
		}
	}

	return c;
}

static void s_client_free(client_t *c)
{
	if (c) return;

	cw_cert_destroy(c->client_cert);
	cw_cert_destroy(c->master_cert);

	free(c->filters);

	free(c->endpoint);
	free(c->username);
	free(c->password);

	free(c);
}

static int s_read_config(cw_list_t *cfg, const char *file, int must_exist)
{
	assert(file);
	assert(cfg);

	struct stat st;
	if (stat(file, &st) != 0) {
		if (must_exist) {
			fprintf(stderr, "%s: %s\n", file, strerror(ENOENT));
			return 1;
		}
		return 0;
	}

	FILE *io = fopen(file, "r");
	if (!io) {
		fprintf(stderr, "%s: %s\n", file, strerror(errno));
		return 1;
	}

	if (cw_cfg_read(cfg, io) != 0) {
		fprintf(stderr, "Unable to grok %s\n", file);
		fclose(io);
		return 1;
	}

	fclose(io);
	return 0;
}

static client_t* s_init(int argc, char **argv)
{
	client_t *c = s_client_new();

	const char *short_opts = "+h?Vvqnu:k:t:s:w:c:";
	struct option long_opts[] = {
		{ "help",        no_argument,       NULL, 'h' },
		{ "version",     no_argument,       NULL, 'V' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "quiet",       no_argument,       NULL, 'q' },
		{ "noop",        no_argument,       NULL, 'n' },
		{ "username",    required_argument, NULL, 'u' },
		{ "key",         required_argument, NULL, 'k' },
		{ "timeout",     required_argument, NULL, 't' },
		{ "sleep",       required_argument, NULL, 's' },
		{ "where",       required_argument, NULL, 'w' },
		{ "config",      required_argument, NULL, 'c' },
		{ "optouts",     no_argument,       NULL,  1  },
		{ "cert",        required_argument, NULL, '2' },
		{ 0, 0, 0, 0 },
	};
	int verbose = LOG_WARNING, noop = 0;
	struct stringlist *filters = stringlist_new(NULL);

	cw_log_open("cw-mesh", "stderr");
	cw_log_level(LOG_ERR, NULL);

	char *x, *config_file = NULL;
	int opt, idx = 0;
	while ( (opt = getopt_long(argc, argv, short_opts, long_opts, &idx)) != -1) {
		switch (opt) {
		case 'h':
		case '?':
			printf("cw-mesh, part of clockwork v%s\n", PACKAGE_VERSION);
			exit(0);

		case 'V':
			printf("cw-mesh (Clockwork) %s\n"
			       "Copyright (C) 2014 James Hunt\n",
			       PACKAGE_VERSION);
			exit(0);

		case 'v':
			verbose++;
			if (verbose > LOG_DEBUG) verbose = LOG_DEBUG;
			cw_log_level(verbose, NULL);
			break;

		case 'q':
			verbose = LOG_WARNING;
			cw_log_level(verbose, NULL);
			break;

		case 'n':
			noop = 1;
			break;

		case 'u':
			free(c->username);
			c->username = strdup(optarg);
			break;

		case 'k':
			free(c->authkey);
			c->authkey = strcmp(optarg, "0") == 0 ? NULL : strdup(optarg);
			break;

		case 't':
			c->timeout_ms = atoi(optarg) * 1000;
			if (c->timeout_ms <=    0) c->timeout_ms = 40 * 1000;
			if (c->timeout_ms <  1000) c->timeout_ms =  1 * 1000;
			break;

		case 's':
			c->sleep_ms = atoi(optarg);
			if (c->sleep_ms <=   0) c->sleep_ms = 250;
			if (c->sleep_ms <  100) c->sleep_ms = 100;
			break;

		case 'w':
			x = cw_string("%s\n", optarg);
			stringlist_add(filters, x);
			free(x);
			break;

		case 'c':
			free(config_file);
			config_file = strdup(optarg);
			break;

		case 1:
			SHOW_OPTOUTS = 1;
			break;

		case 2:
			cw_cert_destroy(c->master_cert);
			c->master_cert = cw_cert_read(optarg);
			if (!c->master_cert) {
				fprintf(stderr, "%s: %s", optarg, errno == EINVAL
					? "Invalid Clockwork certificate"
					: strerror(errno));
				exit(1);
			}
			break;

		default:
			fprintf(stderr, "Unknown flag '-%c' (optind %i / optarg '%s')\n", opt, optind, optarg);
			exit(1);
		}
	}

	cw_log_level(verbose, NULL);
	c->filters = stringlist_join(filters, "");

	LIST(config);
	cw_cfg_set(&config, "mesh.timeout",  "40");
	cw_cfg_set(&config, "mesh.sleep",   "250");

	if (!config_file) {
		if (s_read_config(&config, CW_MTOOL_CONFIG_FILE, 0) != 0)
			exit(1);

		if (getenv("HOME")) {
			char *path = cw_string("%s/.cwrc", getenv("HOME"));
			if (s_read_config(&config, path, 0) != 0)
				exit(1);

			config_file = cw_string("%s or %s", CW_MTOOL_CONFIG_FILE, path);
			free(path);

		} else {
			config_file = strdup(CW_MTOOL_CONFIG_FILE);
		}

	} else if (s_read_config(&config, config_file, 1) != 0) {
		exit(1);
	}

	if (c->timeout_ms == 0)
		c->timeout_ms = atoi(cw_cfg_get(&config, "mesh.timeout")) * 1000;
	if (c->timeout_ms < 1000)
		c->timeout_ms = 1000;

	if (c->sleep_ms == 0)
		c->sleep_ms = atoi(cw_cfg_get(&config, "mesh.sleep"));
	if (c->sleep_ms < 100)
		c->sleep_ms = 100;

	c->endpoint = cw_cfg_get(&config, "mesh.master");
	if (!c->endpoint) {
		fprintf(stderr, "mesh.master not found in %s\n", config_file);
		exit(1);
	}
	c->endpoint = cw_string("tcp://%s", c->endpoint);

	if (!c->master_cert) {
		if (!cw_cfg_get(&config, "mesh.cert")) {
			fprintf(stderr, "mesh.cert not found in %s, and --cert not specified\n",
				config_file);
			exit(1);
		}

		c->master_cert = cw_cert_read(cw_cfg_get(&config, "mesh.cert"));
		if (!c->master_cert) {
			fprintf(stderr, "%s: %s\n", cw_cfg_get(&config, "mesh.cert"),
				errno == EINVAL ? "Invalid Clockwork certificate" : strerror(errno));
			exit(1);
		}
		if (!c->master_cert->pubkey) {
			fprintf(stderr, "%s: no public key component found\n",
				cw_cfg_get(&config, "mesh.cert"));
			exit(1);
		}
	}

	if (c->authkey) {
		c->auth_cert = cw_cert_read(c->authkey);
		if (!c->auth_cert) {
			fprintf(stderr, "%s: %s\n", c->authkey,
				errno == EINVAL ? "Invalid Clockwork key" : strerror(errno));
			exit(1);
		}
		if (c->auth_cert->type != CW_CERT_TYPE_SIGNING) {
			fprintf(stderr, "%s: Incorrect key/certificate type (not a %%sig v1 key)\n", c->authkey);
			exit(1);
		}
		if (!c->auth_cert->seckey) {
			fprintf(stderr, "%s: no secret key component found\n", c->authkey);
			exit(1);
		}
	}

	cw_log(LOG_DEBUG, "Parsing command from what's left of argv");
	cmd_destroy(c->command);
	c->command = cmd_parsev((const char **)argv + optind, COMMAND_LITERAL);
	if (!c->command) {
		cw_log(LOG_ERR, "Failed to parse command!");
		exit(2);
	}

	if (noop) exit(0);

	cw_log(LOG_INFO, "Running command `%s`", c->command->string);

	return c;
}

int main(int argc, char **argv)
{
	client_t *c = s_init(argc, argv);
	int rc;

	cw_log(LOG_DEBUG, "Setting up 0MQ context");
	void *zmq = zmq_ctx_new();
	void *zap = cw_zap_startup(zmq, NULL);

	void *client = zmq_socket(zmq, ZMQ_DEALER);

	rc = zmq_setsockopt(client, ZMQ_CURVE_SECRETKEY, cw_cert_secret(c->client_cert), 32);
	assert(rc == 0);
	rc = zmq_setsockopt(client, ZMQ_CURVE_PUBLICKEY, cw_cert_public(c->client_cert), 32);
	assert(rc == 0);
	rc = zmq_setsockopt(client, ZMQ_CURVE_SERVERKEY, cw_cert_public(c->master_cert), 32);
	assert(rc == 0);
	cw_log(LOG_INFO, "Connecting to %s", c->endpoint);
	rc = zmq_connect(client, c->endpoint);
	assert(rc == 0);


	if (!c->username)
		c->username = s_prompt("Username: ", PROMPT_ECHO);
	if (!c->auth_cert)
		c->password = s_prompt("Password: ", PROMPT_NOECHO);

	cw_pdu_t *pdu = cw_pdu_make(NULL, 2, "REQUEST", c->username);
	if (c->auth_cert) {
		assert(c->auth_cert);

		unsigned char unsealed[256 - 64];
		randombytes(unsealed, 256 - 64);

		uint8_t *sealed;
		unsigned long long slen;
		slen = cw_cert_seal(c->auth_cert, unsealed, 256 - 64, &sealed);
		assert(slen == 256);

		char *pubkey = cw_cert_public_s(c->auth_cert);
		cw_pdu_extend(pdu, cw_frame_new(pubkey));
		cw_pdu_extend(pdu, cw_frame_newbuf((const char*)sealed, 256));

		free(pubkey);
		free(sealed);
	} else {
		assert(c->password);
		cw_pdu_extend(pdu, cw_frame_new(""));
		cw_pdu_extend(pdu, cw_frame_new(c->password));
	}
	cw_pdu_extend(pdu, cw_frame_new(c->command->string));
	cw_pdu_extend(pdu, cw_frame_new(c->filters));

	rc = cw_pdu_send(client, pdu);
	assert(rc == 0);

	cw_pdu_t *reply = cw_pdu_recv(client);
	if (strcmp(reply->type, "ERROR") == 0) {
		cw_log(LOG_ERR, "ERROR: %s", cw_pdu_text(reply, 1));
		exit(3);
	}
	if (strcmp(reply->type, "SUBMITTED") != 0) {
		cw_log(LOG_ERR, "Unknown response: %s", reply->type);
		exit(3);
	}

	char *serial = cw_pdu_text(reply, 1);
	cw_pdu_destroy(reply);
	cw_log(LOG_INFO, "query submitted; serial = %s", serial);

	int n;
	for (n = c->timeout_ms / c->sleep_ms; n > 0; n--) {
		cw_sleep_ms(c->sleep_ms);

		cw_log(LOG_INFO, "awaiting result, countdown = %i", n);
		pdu = cw_pdu_make(NULL, 2, "CHECK", serial);
		rc = cw_pdu_send(client, pdu);
		assert(rc == 0);
		cw_pdu_destroy(pdu);

		for (;;) {
			reply = cw_pdu_recv(client);
			if (strcmp(reply->type, "DONE") == 0) {
				cw_pdu_destroy(reply);
				break;
			}

			if (strcmp(reply->type, "ERROR") == 0) {
				cw_log(LOG_ERR, "ERROR: %s", cw_pdu_text(reply, 1));
				cw_pdu_destroy(reply);
				break;
			}

			if (strcmp(reply->type, "OPTOUT") == 0) {
				if (SHOW_OPTOUTS)
					printf("%s %s optout\n", cw_pdu_text(reply, 1),
					                         cw_pdu_text(reply, 2));

			} else if (strcmp(reply->type, "RESULT") == 0) {
				printf("%s %s %s %s", cw_pdu_text(reply, 1),
				                      cw_pdu_text(reply, 2),
				                      cw_pdu_text(reply, 3),
				                      cw_pdu_text(reply, 4));

			} else {
				cw_log(LOG_ERR, "Unknown response: %s", reply->type);
				cw_pdu_destroy(reply);
				break;
			}

			cw_pdu_destroy(reply);
		}
	}

	cw_log(LOG_INFO, "cw-mesh shutting down");


	cw_zmq_shutdown(client, 500);
	cw_zap_shutdown(zap);
	cw_cert_destroy(c->client_cert);
	cw_cert_destroy(c->master_cert);
	s_client_free(c);
	return 0;
}