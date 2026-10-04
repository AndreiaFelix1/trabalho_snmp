#ifndef TLS_H
#define TLS_H

#include <openssl/ssl.h>

#define TLS_CERT_DIR "certs"
#define TLS_CA_FILE "certs/ca.crt"
#define TLS_AGENT_CERT "certs/agent.crt"
#define TLS_AGENT_KEY "certs/agent.key"
#define TLS_MANAGER_CERT "certs/manager.crt"
#define TLS_MANAGER_KEY "certs/manager.key"

SSL_CTX *tls_server_context(const char *cert_file, const char *key_file);
SSL_CTX *tls_client_context(const char *cert_file, const char *key_file);
SSL *tls_accept(SSL_CTX *ctx, int fd);
SSL *tls_connect(SSL_CTX *ctx, int fd);
void tls_cleanup(SSL_CTX *ctx, SSL *ssl);
void tls_init(void);

#endif
