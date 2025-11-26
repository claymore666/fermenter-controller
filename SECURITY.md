# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

We take security vulnerabilities seriously, especially given this project controls physical equipment (heating, cooling, pressure systems).

### How to Report

**Please do NOT report security vulnerabilities through public GitHub issues.**

Instead, report vulnerabilities via one of these methods:

1. **GitHub Security Advisories** (Preferred)
   - Go to the [Security tab](https://github.com/claymore666/fermenter-controller/security)
   - Click "Report a vulnerability"
   - Provide detailed information

2. **Private Contact**
   - Contact the maintainer directly via GitHub

### What to Include

- Type of vulnerability (e.g., authentication bypass, buffer overflow, injection)
- Location of the affected code (file path, line numbers)
- Step-by-step instructions to reproduce
- Proof-of-concept or exploit code (if available)
- Potential impact and attack scenarios

### Response Timeline

- **Initial Response:** Best effort, typically within 1-2 weeks
- **Status Update:** As progress is made
- **Fix Timeline:** Depends on severity and maintainer availability

This is a hobby project maintained in spare time. Response times may vary.

| Severity | Target Timeline |
|----------|-----------------|
| Critical | As soon as possible |
| High     | Next release    |
| Medium   | Future release  |
| Low      | Backlog         |

## Security Measures

### Network Security

- **HTTPS Only:** All HTTP traffic redirects to HTTPS
- **TLS 1.3:** Modern encryption with TLS 1.2 fallback
- **Per-Device Certificates:** Each device generates unique RSA-2048 certificates
- **No Default Passwords:** First-boot requires password creation
- **Session Tokens:** Authentication uses secure random tokens
- **Rate Limiting:** Failed login attempts are rate-limited

### Authentication

- **Password Requirements:** Minimum 8 characters, complexity requirements
- **SHA-256 Hashing:** Passwords are hashed before storage
- **Session Management:** Tokens expire and can be invalidated

### Input Validation

- **Bounds Checking:** All buffer operations use bounds-checked functions
- **Input Sanitization:** User inputs are validated and sanitized
- **No SQL/Command Injection:** No dynamic query or command construction

### Physical Security Considerations

This firmware controls physical equipment. Security failures could result in:

- Equipment damage (over-temperature, over-pressure)
- Product spoilage
- Safety hazards

**Recommendations for Deployment:**

1. Deploy on isolated network segment (VLAN)
2. Use firewall rules to restrict access
3. Enable WiFi WPA3 if available
4. Regularly update firmware
5. Monitor device logs for anomalies
6. Physical access control to the device

## Known Security Limitations

### Self-Signed Certificates

Devices use self-signed certificates, which browsers will warn about. For production deployments:

- Add device certificates to trusted store
- Consider deploying behind a reverse proxy with valid certificates

### Local Network Trust Model

The device trusts all clients on the local network. It is designed for use on trusted networks, not direct internet exposure.

**Do NOT expose this device directly to the internet.**

### No Firmware Signing

Currently, firmware updates are not cryptographically signed. Physical access to the device allows firmware replacement.

## Security Checklist for Contributors

When contributing code, ensure:

- [ ] No hardcoded credentials or secrets
- [ ] All user inputs are validated
- [ ] Buffer operations use bounds checking
- [ ] Sensitive data is not logged
- [ ] New endpoints require authentication
- [ ] Error messages don't leak sensitive information
- [ ] Dependencies are from trusted sources

## Automated Security Scanning

This repository uses:

- **CodeQL:** Static analysis for C/C++ and JavaScript vulnerabilities
- **Dependabot:** Dependency vulnerability monitoring (when applicable)

Security scan results are visible in the [Security tab](https://github.com/claymore666/fermenter-controller/security).

## Acknowledgments

We appreciate responsible disclosure and will acknowledge security researchers who report valid vulnerabilities (unless they prefer to remain anonymous).

---

*Last updated: November 2025*
