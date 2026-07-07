#ifndef REAL_BOARD_TLS_H
#define REAL_BOARD_TLS_H

#include "config.h"
#include "stdint.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#define REAL_BOARD_TLS_MODE_PLAIN 0U
#define REAL_BOARD_TLS_MODE_SERVER_AUTH 1U
#define REAL_BOARD_TLS_MODE_MUTUAL 2U

extern volatile int32_t real_board_tls_last_error;
extern volatile uint8_t real_board_tls_last_step;

uint8_t real_board_tls_is_enabled(const system_config_t *config);
uint8_t real_board_tls_effective_mode(const system_config_t *config);
int real_board_tls_setup(const system_config_t *config,
                         mbedtls_ssl_context *ssl,
                         mbedtls_ssl_config *conf,
                         mbedtls_x509_crt *ca_cert,
                         mbedtls_x509_crt *client_cert,
                         mbedtls_pk_context *client_key,
                         mbedtls_ctr_drbg_context *ctr_drbg);

#endif
