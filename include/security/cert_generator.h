#pragma once

/**
 * Certificate Generator for ESP32 Fermentation Controller
 *
 * Generates per-device self-signed X.509 certificates using mbedtls:
 * - RSA-2048 key pair generation
 * - Self-signed certificate with device serial
 * - 10-year validity
 * - Stores in NVS for persistence
 */

#include <cstring>
#include <cstdint>

#ifdef ESP32_BUILD
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/oid.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <time.h>

// Certificate generation requires mbedtls X509 write functions to be enabled
// and linked. ESP-IDF doesn't enable these by default.
// To enable per-device certificate generation:
// 1. Enable CONFIG_MBEDTLS_X509_CRT_WRITE_C in menuconfig
// 2. Add -DCERT_GENERATION_ENABLED to build_flags
// Without this, a default shared certificate is used (still provides encryption)
#ifndef CERT_GENERATION_ENABLED
#define CERT_GENERATION_DISABLED 1
#endif
#endif

namespace security {

// Certificate and key buffer sizes (PEM format)
static constexpr size_t CERT_PEM_MAX_SIZE = 2048;
static constexpr size_t KEY_PEM_MAX_SIZE = 2048;

// RSA key size in bits
static constexpr int RSA_KEY_BITS = 2048;

// Certificate validity in years
static constexpr int CERT_VALIDITY_YEARS = 10;

/**
 * Certificate generation result
 */
struct CertGeneratorResult {
    bool success;
    char error_msg[64];

    CertGeneratorResult() : success(false) {
        error_msg[0] = '\0';
    }

    void set_error(const char* msg) {
        success = false;
        strncpy(error_msg, msg, sizeof(error_msg) - 1);
        error_msg[sizeof(error_msg) - 1] = '\0';
    }
};

#ifdef ESP32_BUILD

// Default certificate for fallback (when mbedtls write functions not enabled)
// Generated with: openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 3650 -nodes -subj "/CN=fermenter.local"
static const char DEFAULT_CERT[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDFTCCAf2gAwIBAgIUBuBzOu5vZ8SUDNKI+nyg+I9GJ5owDQYJKoZIhvcNAQEL\n"
    "BQAwGjEYMBYGA1UEAwwPZmVybWVudGVyLmxvY2FsMB4XDTI1MTEyNDIxNDM0Nlow\n"
    "XDTzNTExMjIyMTQzNDZaMBoxGDAWBgNVBAMMD2Zlcm1lbnRlci5sb2NhbDCCASIw\n"
    "DQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAM07rv6M+8IVbhj6Or5Up534h1Zc\n"
    "W63SHW/rqU+IFeOk0uHXefdVdLDP0lLvbZNNF4IxzBJsJzFBSvJvnYAu+364Goz5\n"
    "wCCXISbQpFbue3TJdohTWQNKPt+HWK1b0YAv1+lfYrDxc2wsLE2Zz/aIKqWjT49c\n"
    "YqdN9WNs2r9jnX4fC4/koIn7c6RxwyS5SLgBUL2URBRmKLzyfAeJkQ//e1JyMYwg\n"
    "CGtlJn5DFkJDZufqDizsQ7zLsZ6EI5xjQda2U1Qe9Cxpvpzpn1HGtcdcnGfQ/tOs\n"
    "e5gW9/XdLCoSsYBX6Z9cDICN+SH30gcZcAj632TlM+zY4Vkaoxy+qLnfQAMCAwEA\n"
    "AaNTMFEwHQYDVR0OBBYEFAd7jSZcAAVR6qO4Fyzf7nyb8GP5MB8GA1UdIwQYMBaA\n"
    "FAd7jSZcAAVR6qO4Fyzf7nyb8GP5MA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcN\n"
    "AQELBQADggEBAGmA4DvFUqCofxuZHW6Gat1ntGgafq4dVQ2PPt0Q3L79AxIRSvoN\n"
    "+Ru52rJNI2VbgdQoJ8BT5K5IznrA7I1/K2+sMkvrf+5O9hUIwOjsvop8xvAvWLDf\n"
    "ZvBZxqfR0wzSMSZjjAVtOthRa9hPQVLMdvNvdirnMUKBun5CMHYcrgnJNUDI2uuS\n"
    "0NWsvUMKL02hiag40snv28/GKwR3a2sEuBJ0Ne4AO0+DwUrzUyf2UPfolA73bS8C\n"
    "PPmgsCnUVKhzC0u6yZH57U4qrE4ZXfhSVnnF4bDLT4+EEs5mPTacBR5ygo5dyuHE\n"
    "akio2VqAQgz58sbZGFVRK457eQM6hNUbEKU=\n"
    "-----END CERTIFICATE-----\n";

static const char DEFAULT_KEY[] =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDNO67+jPvCFW4Y\n"
    "+jq+VKed+IdWXFut0h1v66lPiBXjpNLh13n3VXSwz9JS722TTReCMcwSbCcxQUry\n"
    "b52ALvt+uBqM+cAglyEm0KRW7nt0yXaIU1kDSj7fh1itW9GAL9fpX2Kw8XNsLCxN\n"
    "mc/2iCqlo0+PXGKnTfVjbNq/Y51+HwuP5KCJ+3OkccMkuUi4AVC9lEQUZii88nwH\n"
    "iZEP/3tScjGMIAhrZSZ+QxZCQ2bn6g4s7EO8y7GehCOcY0HWtlNUHvQsab6c6R9R\n"
    "xrXHXJxn0P7TrHuYFvf13SwqErGAV+mfXAyAjfkh99IHGXAI+t9k5TPs2OFZGqMV\n"
    "fqi530ADAgMBAAECggEATuq6zR4INubHqaoayZJCy99Dp0Unaazobd94AOGKzTjV\n"
    "cEtLlnxRPSYEsahogaI8vm8IkjLtVSbOu2+I6D/orB798qScqMuET2keMGFOrqdD\n"
    "QYOPMFEt4QUp56ttYTXEd/QaPoDxybAorwRMr/dHMt8b2jwDoWK8T6mNadfTuSlk\n"
    "rvIBoamospyEYYz7UbbGssiUtLonz5InwKGBGp8nzNFemHE5AnkUz2QsTHSfi+zJ\n"
    "l9/5gB3d+OE2KZmceMGgpQ1oyv7i1/weKERxnP95a8G4YgiSNcdRrq3ArY2Iqp9e\n"
    "I+Y5DNO68AnROULgM9AQ1TSmvlZ7BcvSgQLGSbYlTQKBgQD+MhnFVBR9nReu70Yn\n"
    "atTUXBMrSNJkJ2GWG8VIlb8o+iiUmyMcdT3VWYdrs5rz/502clgad+h9HcJ9cZrI\n"
    "ToKSzEG/PIPj2qSAzysyX+kiV+UgiX/2iD6fxjJd+yU13nCywllSqAeyfFEeDOsF\n"
    "it2ALOykColdPx62Bfw8wV7MZwKBgQDOsJzrLEd0CEjGIn1/iPnfM9LZBFBi0PaY\n"
    "0eU629dzbSgSCmbA9q0CrzL2A3Tb1uZ5mnOj02M7bGmAy7oa0gIIO50WdRg8xQgT\n"
    "9tm5r9HN/VcIx8EM24iQm1AOPrY9FwFZexqo85/nOvlQiirTR/EQZVbO7Y8UUHIO\n"
    "dqlSIKduBQKBgQDOuI3OtUcItKWbBUnHKpE0tkB8lfdLrd8lxSXWlrlkKLSxzcxr\n"
    "C0mi5PFFfEXKopkGu0y9EcDHZ1lQzP+0YGy911CsphkYRyo6+r/FcsxUuqhCoq+n\n"
    "HTvYkcVKOsETIvgB2B3uI2pHE+SgDJ9g3YKvB1nXOh5l77wZCZsNCbD/hwKBgGJY\n"
    "OKHRdMIp+u6DlLEtLK9eSjHGUrVh9iOqo2aJGg+q3YkP9+pStOl1EUtrQ5wiuZEc\n"
    "w28s8qdgoyaMSSXfzOW777eyyXCI05okN16Z4LshktrzqNCEWIttyv6sKiwRGSxJ\n"
    "XdsL6IauUdhXlZ7oOTRy84YMFKs2x75ICbKxJNk9AoGAUpSQeF6D9TuDilR4Z2hS\n"
    "wILZsopuCXnREoEo/8/2EPcryUA4zmymDFIhW9z+VyBK60Hzhhlf2aeLOpNswZnd\n"
    "JTRXNBQFS2+RP2gsR1RNe15AnBYJDZ+4TxLDEjBYz0qRzA1it7WozqIpQWV6NzmJ\n"
    "RGe4P8Wo/5TVZQIdZgdEANo=\n"
    "-----END PRIVATE KEY-----\n";

#ifdef CERT_GENERATION_DISABLED
// Certificate generation not available - use default certificate
// To enable per-device certificates, add to sdkconfig:
//   CONFIG_MBEDTLS_X509_CRT_WRITE_C=y
//   CONFIG_MBEDTLS_X509_CREATE_C=y
//   CONFIG_MBEDTLS_PK_WRITE_C=y

/**
 * Generate self-signed certificate - FALLBACK VERSION
 * Uses default certificate since mbedtls write functions not enabled
 */
inline CertGeneratorResult generate_self_signed_cert(
    char* cert_pem, size_t cert_size, size_t* cert_len,
    char* key_pem, size_t key_size, size_t* key_len)
{
    CertGeneratorResult result;

    if (!cert_pem || !key_pem || !cert_len || !key_len) {
        result.set_error("Null parameters");
        return result;
    }

    ESP_LOGW("CERT", "Certificate generation not available (mbedtls write functions disabled)");
    ESP_LOGW("CERT", "Using default certificate - all devices share same cert/key");
    ESP_LOGW("CERT", "To enable per-device certs, add CONFIG_MBEDTLS_X509_CRT_WRITE_C=y to sdkconfig");

    if (cert_size < sizeof(DEFAULT_CERT) || key_size < sizeof(DEFAULT_KEY)) {
        result.set_error("Buffer too small");
        return result;
    }

    memcpy(cert_pem, DEFAULT_CERT, sizeof(DEFAULT_CERT));
    *cert_len = sizeof(DEFAULT_CERT);

    memcpy(key_pem, DEFAULT_KEY, sizeof(DEFAULT_KEY));
    *key_len = sizeof(DEFAULT_KEY);

    result.success = true;
    return result;
}

#else
// Full certificate generation available

/**
 * Generate self-signed certificate and private key
 *
 * @param cert_pem Output buffer for certificate (PEM format)
 * @param cert_size Size of cert_pem buffer
 * @param cert_len Output: actual length written (including null)
 * @param key_pem Output buffer for private key (PEM format)
 * @param key_size Size of key_pem buffer
 * @param key_len Output: actual length written (including null)
 * @return Result with success status and error message
 */
inline CertGeneratorResult generate_self_signed_cert(
    char* cert_pem, size_t cert_size, size_t* cert_len,
    char* key_pem, size_t key_size, size_t* key_len)
{
    CertGeneratorResult result;

    if (!cert_pem || !key_pem || !cert_len || !key_len) {
        result.set_error("Null parameters");
        return result;
    }

    if (cert_size < CERT_PEM_MAX_SIZE || key_size < KEY_PEM_MAX_SIZE) {
        result.set_error("Buffer too small");
        return result;
    }

    ESP_LOGI("CERT", "Generating RSA-%d key pair (this may take 30-60 seconds)...", RSA_KEY_BITS);

    // Initialize mbedtls structures
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&crt);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    int ret;
    char mbedtls_error[100];

    // Seed the random number generator with device MAC for uniqueness
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char seed[32];
    snprintf(seed, sizeof(seed), "fermenter_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)seed, strlen(seed));
    if (ret != 0) {
        mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
        ESP_LOGE("CERT", "DRBG seed failed: %s", mbedtls_error);
        result.set_error("RNG init failed");
        goto cleanup;
    }

    // Generate RSA key pair
    ESP_LOGI("CERT", "Generating RSA key...");
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
        ESP_LOGE("CERT", "PK setup failed: %s", mbedtls_error);
        result.set_error("Key setup failed");
        goto cleanup;
    }

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg,
                               RSA_KEY_BITS, 65537);
    if (ret != 0) {
        mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
        ESP_LOGE("CERT", "RSA keygen failed: %s", mbedtls_error);
        result.set_error("Key generation failed");
        goto cleanup;
    }
    ESP_LOGI("CERT", "RSA key generated successfully");

    // Build subject name with device MAC
    {
        char subject[128];
        snprintf(subject, sizeof(subject),
                 "CN=fermenter-%02X%02X%02X,O=Brewery Controller,OU=Fermentation",
                 mac[3], mac[4], mac[5]);

        // Set up certificate
        mbedtls_x509write_crt_set_subject_key(&crt, &key);
        mbedtls_x509write_crt_set_issuer_key(&crt, &key);  // Self-signed

        ret = mbedtls_x509write_crt_set_subject_name(&crt, subject);
        if (ret != 0) {
            mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
            ESP_LOGE("CERT", "Set subject failed: %s", mbedtls_error);
            result.set_error("Subject name failed");
            goto cleanup;
        }

        ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject);  // Self-signed
        if (ret != 0) {
            mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
            ESP_LOGE("CERT", "Set issuer failed: %s", mbedtls_error);
            result.set_error("Issuer name failed");
            goto cleanup;
        }
    }

    // Set serial number (based on MAC + timestamp for uniqueness)
    // Use 16-byte format: 0x01 prefix (ensures positive) + MAC (6) + timestamp (4) + random (5)
    {
        unsigned char serial_bytes[16];
        serial_bytes[0] = 0x01;  // Ensure positive integer (ASN.1 two's complement)

        // MAC address (bytes 1-6)
        memcpy(&serial_bytes[1], mac, 6);

        // Timestamp lower 32 bits (bytes 7-10)
        uint32_t ts = (uint32_t)time(nullptr);
        serial_bytes[7] = (ts >> 24) & 0xFF;
        serial_bytes[8] = (ts >> 16) & 0xFF;
        serial_bytes[9] = (ts >> 8) & 0xFF;
        serial_bytes[10] = ts & 0xFF;

        // Random bytes for uniqueness (bytes 11-15)
        mbedtls_ctr_drbg_random(&ctr_drbg, &serial_bytes[11], 5);

        ret = mbedtls_x509write_crt_set_serial_raw(&crt, serial_bytes, sizeof(serial_bytes));
        if (ret != 0) {
            result.set_error("Set serial failed");
            goto cleanup;
        }
    }

    // Set validity period
    {
        time_t now = time(nullptr);
        struct tm not_before, not_after;

        gmtime_r(&now, &not_before);

        // Set not_after to CERT_VALIDITY_YEARS from now
        not_after = not_before;
        not_after.tm_year += CERT_VALIDITY_YEARS;

        char not_before_str[16], not_after_str[16];
        strftime(not_before_str, sizeof(not_before_str), "%Y%m%d%H%M%S", &not_before);
        strftime(not_after_str, sizeof(not_after_str), "%Y%m%d%H%M%S", &not_after);

        ret = mbedtls_x509write_crt_set_validity(&crt, not_before_str, not_after_str);
        if (ret != 0) {
            mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
            ESP_LOGE("CERT", "Set validity failed: %s", mbedtls_error);
            result.set_error("Validity period failed");
            goto cleanup;
        }
    }

    // Set extensions
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);

    // Basic constraints: CA=true (self-signed root)
    ret = mbedtls_x509write_crt_set_basic_constraints(&crt, 1, -1);
    if (ret != 0) {
        result.set_error("Basic constraints failed");
        goto cleanup;
    }

    // Key usage: digital signature, key encipherment
    ret = mbedtls_x509write_crt_set_key_usage(&crt,
        MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT);
    if (ret != 0) {
        result.set_error("Key usage failed");
        goto cleanup;
    }

    // Subject Alternative Name (SAN) - DNS name for browser compatibility
    // Format: fermenter-XXXXXX.local (device-specific mDNS hostname)
    {
        char dns_name[32];
        snprintf(dns_name, sizeof(dns_name), "fermenter-%02X%02X%02X.local",
                 mac[3], mac[4], mac[5]);
        size_t dns_len = strlen(dns_name);

        // ASN.1 DER encoding for SAN with DNS name:
        // SEQUENCE { [2] IA5String "hostname" }
        // Tag 0x82 = context-specific [2] for dNSName
        unsigned char san_buf[64];
        size_t san_len = 0;

        // GeneralName: dNSName [2] IMPLICIT IA5String
        san_buf[san_len++] = 0x82;  // Context tag [2] for dNSName
        san_buf[san_len++] = (unsigned char)dns_len;
        memcpy(&san_buf[san_len], dns_name, dns_len);
        san_len += dns_len;

        // Wrap in SEQUENCE (GeneralNames)
        unsigned char san_ext[72];
        san_ext[0] = 0x30;  // SEQUENCE tag
        san_ext[1] = (unsigned char)san_len;
        memcpy(&san_ext[2], san_buf, san_len);
        size_t san_ext_len = 2 + san_len;

        // OID for Subject Alternative Name: 2.5.29.17
        ret = mbedtls_x509write_crt_set_extension(&crt,
            MBEDTLS_OID_SUBJECT_ALT_NAME, MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME),
            0,  // not critical
            san_ext, san_ext_len);
        if (ret != 0) {
            mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
            ESP_LOGW("CERT", "Failed to set SAN extension: %s", mbedtls_error);
            // Continue without SAN - cert will still work but browser will warn about name mismatch
        } else {
            ESP_LOGI("CERT", "Added SAN: %s", dns_name);
        }
    }

    // Write certificate to PEM
    ESP_LOGI("CERT", "Writing certificate...");
    ret = mbedtls_x509write_crt_pem(&crt, (unsigned char*)cert_pem, cert_size,
                                     mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
        ESP_LOGE("CERT", "Write cert PEM failed: %s", mbedtls_error);
        result.set_error("Cert PEM write failed");
        goto cleanup;
    }
    *cert_len = strlen(cert_pem) + 1;

    // Write private key to PEM
    ESP_LOGI("CERT", "Writing private key...");
    ret = mbedtls_pk_write_key_pem(&key, (unsigned char*)key_pem, key_size);
    if (ret != 0) {
        mbedtls_strerror(ret, mbedtls_error, sizeof(mbedtls_error));
        ESP_LOGE("CERT", "Write key PEM failed: %s", mbedtls_error);
        result.set_error("Key PEM write failed");
        goto cleanup;
    }
    *key_len = strlen(key_pem) + 1;

    ESP_LOGI("CERT", "Certificate generated successfully (cert=%zu bytes, key=%zu bytes)",
             *cert_len, *key_len);
    result.success = true;

cleanup:
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return result;
}

#endif // CERT_GENERATION_DISABLED

/**
 * Get device serial string (based on MAC address)
 *
 * @param buffer Output buffer
 * @param size Buffer size
 * @return Length written (excluding null)
 */
inline size_t get_device_serial(char* buffer, size_t size) {
    if (!buffer || size < 18) return 0;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    return snprintf(buffer, size, "%02X%02X%02X%02X%02X%02X",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

#else
// Simulator stubs

inline CertGeneratorResult generate_self_signed_cert(
    char* cert_pem, size_t cert_size, size_t* cert_len,
    char* key_pem, size_t key_size, size_t* key_len)
{
    CertGeneratorResult result;

    // For simulator, just return test certificates
    static const char* TEST_CERT =
        "-----BEGIN CERTIFICATE-----\n"
        "MIIBkTCB+wIJAKHBfpegPgLCMA0GCSqGSIb3DQEBCwUAMBExDzANBgNVBAMMBnNp\n"
        "bXVsYTAeFw0yNTAxMDEwMDAwMDBaFw0zNTAxMDEwMDAwMDBaMBExDzANBgNVBAMM\n"
        "BnNpbXVsYTBcMA0GCSqGSIb3DQEBAQUAA0sAMEgCQQC7LM3lPgBqKYCRSmlcG6+7\n"
        "p9WNl/xLyUYM7kYy3/8y7nKPQ+IgqJ1A8mBN8f0dB4b0VQMB8HJTN/G8QvRkLzVH\n"
        "AgMBAAGjUzBRMB0GA1UdDgQWBBT0eEKM7N3xKUJFBYKvqrYNchOCpTAfBgNVHSME\n"
        "GDAWgBT0eEKM7N3xKUJFBYKvqrYNchOCpTAPBgNVHRMBAf8EBTADAQH/MA0GCSqG\n"
        "SIb3DQEBCwUAA0EAl8hHLkKJwqeNLvJcP7l3u7RhQOHBITHJ0MJsYqRUMC/wIb4M\n"
        "cQ8qKYxRYqz8oMvxB5QC6lKNI8IVVQ8dSqcWNA==\n"
        "-----END CERTIFICATE-----\n";

    static const char* TEST_KEY =
        "-----BEGIN PRIVATE KEY-----\n"
        "MIIBVgIBADANBgkqhkiG9w0BAQEFAASCAUAwggE8AgEAAkEAuyzN5T4AaimAkUpp\n"
        "XBuvu6fVjZf8S8lGDO5GMt//Mu5yj0PiIKidQPJgTfH9HQeG9FUDAfByUzfxvEL0\n"
        "ZC81RwIDAQABAkAl7tSqFjpSQN1yE8fGmOvH7HxKF9sZx7KQwMIXl8KMQJpRKliv\n"
        "6e0V7qNHLEAQz8gGGS8fJJEJEyN0p6kJ8qmhAiEA5tP0Zf6dBa4T8R5S7K7I5VpO\n"
        "8oqqMyUBvYZRYdMb4hsCIQDQWJOYjF8N3iLtxdJBOqmJNYMPBg5rLGQJQkCRJfKe\n"
        "JQIhALLjF4P3FIqPvBq8HqHHBJBNPKfWKc+O+9/zT6l7mNWrAiEAy9cP8BYJqFQX\n"
        "8VoVJ1Ey7Ef0hwlJCCyBVKNlJ1X8hPECIQCVJF0nqCdGJ2e8PJBHV8RWMGJJ1Qmp\n"
        "sSp8P7rQJPpzJg==\n"
        "-----END PRIVATE KEY-----\n";

    if (cert_pem && cert_size >= strlen(TEST_CERT) + 1) {
        strcpy(cert_pem, TEST_CERT);
        *cert_len = strlen(TEST_CERT) + 1;
    }

    if (key_pem && key_size >= strlen(TEST_KEY) + 1) {
        strcpy(key_pem, TEST_KEY);
        *key_len = strlen(TEST_KEY) + 1;
    }

    result.success = true;
    return result;
}

inline size_t get_device_serial(char* buffer, size_t size) {
    if (!buffer || size < 13) return 0;
    return snprintf(buffer, size, "SIMULATOR001");
}

#endif // ESP32_BUILD

} // namespace security
