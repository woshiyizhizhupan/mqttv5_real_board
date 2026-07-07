#include "real_board_tls.h"

#ifndef REAL_BOARD_TLS_CA_CERT_PEM
const char REAL_BOARD_TLS_CA_CERT_PEM[] __attribute__((weak)) = "";
#endif

#ifndef REAL_BOARD_TLS_CLIENT_CERT_PEM
const char REAL_BOARD_TLS_CLIENT_CERT_PEM[] __attribute__((weak)) = "";
#endif

#ifndef REAL_BOARD_TLS_CLIENT_KEY_PEM
const char REAL_BOARD_TLS_CLIENT_KEY_PEM[] __attribute__((weak)) = "";
#endif

#define REAL_BOARD_TLS_ERR_MISSING_CA (-0x7100)
#define REAL_BOARD_TLS_ERR_MISSING_CLIENT_CERT (-0x7101)
#define REAL_BOARD_TLS_ERR_MISSING_CLIENT_KEY (-0x7102)

#define REAL_BOARD_TLS_STEP_NONE 0U
#define REAL_BOARD_TLS_STEP_PARSE_CA 1U
#define REAL_BOARD_TLS_STEP_PARSE_CLIENT_CERT 2U
#define REAL_BOARD_TLS_STEP_PARSE_CLIENT_KEY 3U
#define REAL_BOARD_TLS_STEP_OWN_CERT 4U
#define REAL_BOARD_TLS_STEP_DONE 5U

volatile int32_t real_board_tls_last_error = 0;
volatile uint8_t real_board_tls_last_step = REAL_BOARD_TLS_STEP_NONE;

static int record_tls_error(int ret)
{
    real_board_tls_last_error = ret;
    return ret;
}

static uint8_t real_board_tls_pem_is_empty(const char *pem)
{
    const volatile char *cursor = (const volatile char *)pem;
    return cursor[0] == '\0';
}

static size_t real_board_tls_pem_len_with_nul(const char *pem)
{
    const volatile char *cursor = (const volatile char *)pem;
    size_t len = 0U;

    while (cursor[len] != '\0')
        len++;

    return len + 1U;
}

uint8_t real_board_tls_effective_mode(const system_config_t *config)
{
    if (config == NULL)
        return REAL_BOARD_TLS_MODE_PLAIN;

    if (config->mqtt_server.tls_mode <= REAL_BOARD_TLS_MODE_MUTUAL &&
        config->mqtt_server.tls_mode != REAL_BOARD_TLS_MODE_PLAIN)
    {
        return config->mqtt_server.tls_mode;
    }

    if (config->mqtt_server.port == 8884U)
        return REAL_BOARD_TLS_MODE_MUTUAL;

    if (config->mqtt_server.port == 8883U)
        return REAL_BOARD_TLS_MODE_SERVER_AUTH;

    return REAL_BOARD_TLS_MODE_PLAIN;
}

uint8_t real_board_tls_is_enabled(const system_config_t *config)
{
    return real_board_tls_effective_mode(config) != REAL_BOARD_TLS_MODE_PLAIN;
}

static int parse_ca(mbedtls_ssl_config *conf, mbedtls_x509_crt *ca_cert)
{
    int ret;

    if (real_board_tls_pem_is_empty(REAL_BOARD_TLS_CA_CERT_PEM))
        return record_tls_error(REAL_BOARD_TLS_ERR_MISSING_CA);

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_PARSE_CA;
    ret = mbedtls_x509_crt_parse(ca_cert,
                                 (const unsigned char *)REAL_BOARD_TLS_CA_CERT_PEM,
                                 real_board_tls_pem_len_with_nul(REAL_BOARD_TLS_CA_CERT_PEM));
    if (ret != 0)
        return record_tls_error(ret);

    mbedtls_ssl_conf_ca_chain(conf, ca_cert, NULL);
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    real_board_tls_last_error = 0;
    return 0;
}

static int parse_client_identity(mbedtls_ssl_config *conf,
                                 mbedtls_x509_crt *client_cert,
                                 mbedtls_pk_context *client_key,
                                 mbedtls_ctr_drbg_context *ctr_drbg)
{
    int ret;

    if (real_board_tls_pem_is_empty(REAL_BOARD_TLS_CLIENT_CERT_PEM))
        return record_tls_error(REAL_BOARD_TLS_ERR_MISSING_CLIENT_CERT);
    if (real_board_tls_pem_is_empty(REAL_BOARD_TLS_CLIENT_KEY_PEM))
        return record_tls_error(REAL_BOARD_TLS_ERR_MISSING_CLIENT_KEY);

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_PARSE_CLIENT_CERT;
    ret = mbedtls_x509_crt_parse(client_cert,
                                 (const unsigned char *)REAL_BOARD_TLS_CLIENT_CERT_PEM,
                                 real_board_tls_pem_len_with_nul(REAL_BOARD_TLS_CLIENT_CERT_PEM));
    if (ret != 0)
        return record_tls_error(ret);

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_PARSE_CLIENT_KEY;
    ret = mbedtls_pk_parse_key(client_key,
                               (const unsigned char *)REAL_BOARD_TLS_CLIENT_KEY_PEM,
                               real_board_tls_pem_len_with_nul(REAL_BOARD_TLS_CLIENT_KEY_PEM),
                               NULL,
                               0,
                               mbedtls_ctr_drbg_random,
                               ctr_drbg);
    if (ret != 0)
        return record_tls_error(ret);

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_OWN_CERT;
    ret = mbedtls_ssl_conf_own_cert(conf, client_cert, client_key);
    if (ret != 0)
        return record_tls_error(ret);

    real_board_tls_last_error = 0;
    return 0;
}

int real_board_tls_setup(const system_config_t *config,
                         mbedtls_ssl_context *ssl,
                         mbedtls_ssl_config *conf,
                         mbedtls_x509_crt *ca_cert,
                         mbedtls_x509_crt *client_cert,
                         mbedtls_pk_context *client_key,
                         mbedtls_ctr_drbg_context *ctr_drbg)
{
    int ret;
    uint8_t mode = real_board_tls_effective_mode(config);

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_NONE;
    real_board_tls_last_error = 0;

    static const int ciphersuites[] = {
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
        MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        0,
    };

    mbedtls_ssl_conf_ciphersuites(conf, ciphersuites);
    mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_max_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    mbedtls_ssl_conf_rng(conf, mbedtls_ctr_drbg_random, ctr_drbg);

    if (mode == REAL_BOARD_TLS_MODE_PLAIN)
    {
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
        return 0;
    }

    if (config != NULL && config->mqtt_server.tls_verify_peer == 0U)
    {
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    else
    {
        ret = parse_ca(conf, ca_cert);
        if (ret != 0)
            return ret;
    }

    if (config != NULL && config->mqtt_server.host[0] != '\0')
    {
        ret = mbedtls_ssl_set_hostname(ssl, config->mqtt_server.host);
        if (ret != 0)
            return ret;
    }

    if (mode == REAL_BOARD_TLS_MODE_MUTUAL)
    {
        ret = parse_client_identity(conf, client_cert, client_key, ctr_drbg);
        if (ret != 0)
            return ret;
        real_board_tls_last_step = REAL_BOARD_TLS_STEP_DONE;
        return 0;
    }

    real_board_tls_last_step = REAL_BOARD_TLS_STEP_DONE;
    return 0;
}
