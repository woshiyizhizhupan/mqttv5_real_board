#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

// #define MBEDTLS_DEBUG_C // 启用调试功能（可选） 

/* 启用汇编优化 */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_UDBL_DIVISION

/* 平台支持 */
#define MBEDTLS_PLATFORM_C                  // 启用平台抽象层
#define MBEDTLS_ENTROPY_C                   // 启用随机数生成器
#define MBEDTLS_NO_PLATFORM_ENTROPY         // 禁用平台默认的熵源,使用自定义随机源

#define MBEDTLS_SSL_CLI_C                   // 启用SSL客户端
#define MBEDTLS_SSL_TLS_C                   // 启用TLS
#define MBEDTLS_MD_C                        // 启用消息摘要抽象层

/* 启用算法 */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C  // 可选但推荐（TLS1.3 需要）

#define MBEDTLS_SSL_PROTO_TLS1_2                // 启用 TLS 1.2
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED  // 必须支持 ECDHE_RSA
#define MBEDTLS_AES_C                           // 启用 AES
#define MBEDTLS_GCM_C                           // 启用 GCM 模式
#define MBEDTLS_ECDH_C                          // 启用 ECDH
// #define MBEDTLS_ECDSA_C                         // 启用 ECDSA（可选，用于证书验证）


#define MBEDTLS_X509_USE_C                      // 启用证书解析
#define MBEDTLS_X509_CRT_PARSE_C                // 启用 X.509 证书解析

#define MBEDTLS_CIPHER_C                    // 启用 对称加密算法抽象层

#define MBEDTLS_RSA_C                       // 启用非对称密码
#define MBEDTLS_BIGNUM_C                    // 大整数运算（必需）
#define MBEDTLS_OID_C                       // 启动对象标识符（OID）解析
#define MBEDTLS_PKCS1_V15                   // 启用 PKCS#1 v1.5 功能
#define MBEDTLS_ASN1_WRITE_C

#define MBEDTLS_PK_C                        // 启用 公钥密码学
#define MBEDTLS_PK_PARSE_C                  // 启用 公钥（Public Key）解析
#define MBEDTLS_ASN1_PARSE_C                // ASN.1 解析基础
#define MBEDTLS_PKCS12_C                    // PKCS#12 支持

#define MBEDTLS_CERTS_C
#define MBEDTLS_PEM_PARSE_C                 // 启用 PEM 解析
#define MBEDTLS_BASE64_C                    // 提供 Base64 编码和解码的 API

#define MBEDTLS_CTR_DRBG_C                  // 基于 AES-CTR 的确定性随机比特生成器

#define MBEDTLS_ECP_C                       // 启用 椭圆曲线算术
// 启用常用曲线
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

// #define MBEDTLS_SSL_SERVER_NAME_INDICATION    // 启用 SNI 支持
// #define MBEDTLS_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384  // 如果存在则启用

/* 减小缓冲区 */
#define MBEDTLS_SSL_MAX_CONTENT_LEN 2048 // 减小 Mbed TLS 内部缓冲区
#define MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define MBEDTLS_SSL_IN_CONTENT_LEN 4096
#endif /* MBEDTLS_CONFIG_H */


