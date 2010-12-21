#include <stdio.h>
#include <stdlib.h>

#include <glob.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "proto.h"
#include "policy.h"
#include "log.h"
#include "userdb.h"

#define CAFILE   "certs/CA/cacert.pem"

#define USE_JADE 1

#ifdef USE_JADE
#  define CERTFILE "certs/CA/certs/jade.niftylogic.net.pem"
#  define KEYFILE  "certs/jade.niftylogic.net/key.pem"
#else
#  define CERTFILE "certs/CA/certs/client.niftylogic.net.pem"
#  define KEYFILE  "certs/client.niftylogic.net/key.pem"
#endif

#define SERVER "cfm.niftylogic.net"
#define PORT   "7890"

#define FACTS "facts/*"

int get_facts_from_script(const char *script, struct hash *facts)
{
	pid_t pid;
	int pipefd[2];
	FILE *input;
	char *path_copy, *arg0;

	path_copy = strdup(script);
	arg0 = basename(path_copy);
	free(path_copy);

	printf("Procesing script %s\n", script);

	if (pipe(pipefd) != 0) {
		perror("get_facts_from_script");
		return -1;
	}

	pid = fork();
	switch (pid) {
	case -1:
		perror("get_facts_from_script: fork");
		return -1;

	case 0: /* in child */
		close(pipefd[0]);
		close(0); close(1); close(2);

		dup2(pipefd[1], 1); /* dup pipe as stdout */

		execl(script, arg0, NULL);
		exit(1); /* if execl returns, we failed */

	default: /* in parent */
		close(pipefd[1]);
		input = fdopen(pipefd[0], "r");

		fact_read(input, facts);
		waitpid(pid, NULL, 0);
		fclose(input);
		close(pipefd[0]);

		return 0;
	}
}

/* Read a directory of scripts, execute them, and parse the output as facts */
struct hash* get_the_facts(const char *dir)
{
	glob_t scripts;
	size_t i;
	struct hash *facts;

	facts = hash_new();

	switch(glob(dir, GLOB_MARK, NULL, &scripts)) {
	case GLOB_NOMATCH:
		globfree(&scripts);
		if (get_facts_from_script(dir, facts) != 0) {
			hash_free(facts);
			return NULL;
		}
		return facts;

	case GLOB_NOSPACE:
	case GLOB_ABORTED:
		hash_free(facts);
		return NULL;

	}

	for (i = 0; i < scripts.gl_pathc; i++) {
		if (get_facts_from_script(scripts.gl_pathv[i], facts) != 0) {
			hash_free(facts);
			globfree(&scripts);
			return NULL;
		}
	}

	globfree(&scripts);
	return facts;
}

#ifdef DEVEL
static void DEVELOPER_verify_facts(const struct hash *facts)
{
	struct hash_cursor cur;
	char *key, *val;

	fprintf(stderr, "  > dumping facts\n");
	for_each_key_value(facts, &cur, key, val) {
		fprintf(stderr, "     found '%s' = '%s'\n", key, val);
	}
	fprintf(stderr, "\n");
}
#else
#  define DEVELOPER_verify_facts(x)
#endif

static struct policy* get_policy(protocol_session *session, const struct hash *facts)
{
	struct policy *pol;

	if (pdu_send_GET_POLICY(session, facts) < 0) {
		CRITICAL("Unable to GET_POLICY");
		exit(42);
	}

	pdu_receive(session);
	if (RECV_PDU(session)->op != PROTOCOL_OP_SEND_POLICY) {
		CRITICAL("Unexpected op from server: %u", RECV_PDU(session)->op);
		exit(42);
	}

	if (pdu_decode_SEND_POLICY(RECV_PDU(session), &pol) != 0) {
		CRITICAL("Unable to decode SEND_POLICY PDU");
		exit(42);
	}

	return pol;
}


static void dump_policy(struct policy *pol)
{
	char *packed = policy_pack(pol);
	fprintf(stderr, "%s\n", packed);
	free(packed);
}

int policy_enforce(const struct policy *pol)
{
	struct pwdb *passwd;
	struct spdb *shadow;
	struct grdb *group;
	struct sgdb *gshadow;

	struct res_user *ru;
	struct res_group *rg;
	struct res_file *rf;

	passwd  = pwdb_init(SYS_PASSWD);
	shadow  = spdb_init(SYS_SHADOW);
	group   = grdb_init(SYS_GROUP);
	gshadow = sgdb_init(SYS_GSHADOW);
	/* FIXME: check return values of userdb init calls. */

	/* Remediate users */
	for_each_node(ru, &pol->res_users, res) {
		res_user_stat(ru, passwd, shadow);
		res_user_remediate(ru, passwd, shadow);
	}

	/* Remediate groups */
	for_each_node(rg, &pol->res_groups, res) {
		res_group_stat(rg, group, gshadow);
		res_group_remediate(rg, group, gshadow);
	}

	/* Remediate files */
	for_each_node(rf, &pol->res_files, res) {
		res_file_stat(rf);
		res_file_remediate(rf);
	}

	return 0;
}

int main(int argc, char **argv)
{
	BIO *sock;
	SSL *ssl;
	SSL_CTX *ctx;
	long err;
	protocol_session session;

	struct hash *facts;
	struct policy *policy;

	printf("SSL Connection opened\n");
	printf("  > gathering facts\n");
	facts = get_the_facts(FACTS);
	DEVELOPER_verify_facts(facts);

	protocol_ssl_init();
	ctx = protocol_ssl_default_context(CAFILE, CERTFILE, KEYFILE);
	if (!ctx) {
		CRITICAL("Error setting up SSL context");
		protocol_ssl_backtrace();
		exit(1);
	}

	sock = BIO_new_connect(SERVER ":" PORT);
	if (!sock) {
		CRITICAL("Error creating connection BIO");
		protocol_ssl_backtrace();
		exit(1);
	}

	if (BIO_do_connect(sock) <= 0) {
		CRITICAL("Error connecting to remote server");
		protocol_ssl_backtrace();
		exit(1);
	}

	if (!(ssl = SSL_new(ctx))) {
		CRITICAL("Error creating an SSL context");
		protocol_ssl_backtrace();
		exit(1);
	}
	SSL_set_bio(ssl, sock, sock);
	if (SSL_connect(ssl) <= 0) {
		CRITICAL("Error connecting SSL object");
		protocol_ssl_backtrace();
		exit(1);
	}
	if ((err = protocol_ssl_verify_peer(ssl, SERVER)) != X509_V_OK) {
		CRITICAL("Server certificate verification failed: %s", X509_verify_cert_error_string(err));
		protocol_ssl_backtrace();
		exit(1);
	}

	protocol_session_init(&session, ssl, NULL);

	policy = get_policy(&session, facts);
	dump_policy(policy);

	INFO("Enforcing policy on local system");
	policy_enforce(policy);

	/* disconnect */
	if (pdu_send_BYE(&session) < 0) {
		perror("client_disconnect");
		return -1;
	}
	pdu_receive(&session);

	SSL_shutdown(ssl);
	protocol_session_deinit(&session);

	SSL_free(ssl);
	SSL_CTX_free(ctx);
	return 0;
}

