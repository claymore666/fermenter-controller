# mDNS and HTTPS Configuration

Device discovery and secure HTTPS access for the fermentation controller.

## Overview

The controller uses mDNS (Multicast DNS) for device discovery on the local network. Each device is accessible via a unique hostname based on its MAC address:

```
https://fermenter-XXXXXX.local
```

Where `XXXXXX` is the last 3 bytes of the device's WiFi MAC address (uppercase hex).

## mDNS Hostname

| Component | Value | Example |
|-----------|-------|---------|
| Hostname | `fermenter-XXXXXX` | `fermenter-230778` |
| FQDN | `fermenter-XXXXXX.local` | `fermenter-230778.local` |
| DHCP hostname | Same as mDNS | `fermenter-230778` |

The hostname is derived from the WiFi MAC address:
- Last 3 bytes (MAC[3], MAC[4], MAC[5])
- Uppercase hexadecimal format
- Example: MAC `DC:DA:0C:23:07:78` → `fermenter-230778`

### Services Advertised

| Service | Port | Description |
|---------|------|-------------|
| `_http._tcp` | 80 | HTTP (redirects to HTTPS) |
| `_https._tcp` | 443 | HTTPS REST API |

## HTTPS Certificate

Each device generates a unique self-signed RSA-2048 certificate on first boot.

### Certificate Details

| Field | Value |
|-------|-------|
| Subject CN | `fermenter-XXXXXX` |
| Organization | `Brewery Controller` |
| Organizational Unit | `Fermentation` |
| Key Type | RSA-2048 |
| Signature | SHA-256 |
| Validity | 10 years |
| SAN (Subject Alternative Name) | `DNS:fermenter-XXXXXX.local` |

### Certificate Generation

On first boot (or after `ssl clear`):
1. RSA-2048 key pair generated (~9 seconds)
2. Self-signed X.509v3 certificate created
3. SAN extension added with device FQDN
4. Certificate/key stored in NVS
5. HTTPS server started

Status LED shows blue blinking during certificate generation.

## Accessing the Device

### Via mDNS (Recommended)

```bash
# Test mDNS resolution
ping fermenter-230778.local

# Access HTTPS
curl -k https://fermenter-230778.local/api/status

# Access admin interface
https://fermenter-230778.local/admin/
```

### Via IP Address

```bash
# Find IP via mDNS
avahi-resolve -n fermenter-230778.local

# Or check router DHCP table
# Or use debug console: wifi
```

## Browser Certificate Warning

Browsers will show a certificate warning because:
1. Certificate is self-signed (not from a trusted CA)
2. Not automatically trusted by the OS

### To Trust the Certificate

**Windows:**
1. Open `https://fermenter-XXXXXX.local` in Edge/Chrome
2. Click "Advanced" → "Proceed to site"
3. Click the padlock → "Certificate" → "Details" → "Copy to File"
4. Export as DER (.cer)
5. Run: `certutil -addstore "Root" fermenter.cer`

**Linux:**
```bash
# Download certificate
echo | openssl s_client -connect fermenter-230778.local:443 2>/dev/null | \
  openssl x509 > fermenter.crt

# Add to trusted store (Debian/Ubuntu)
sudo cp fermenter.crt /usr/local/share/ca-certificates/
sudo update-ca-certificates
```

**macOS:**
1. Open Keychain Access
2. Drag certificate file to "System" keychain
3. Double-click certificate → Trust → "Always Trust"

## Debug Console Commands

| Command | Description |
|---------|-------------|
| `ssl status` | Show certificate size |
| `ssl clear` | Delete certificate (regenerates on reboot) |
| `wifi` | Show WiFi status including hostname |

## Troubleshooting

### mDNS Not Resolving

1. Ensure device and client are on same network
2. Check mDNS/Bonjour service is running on client:
   - Windows: Bonjour Print Services or iTunes
   - Linux: `avahi-daemon`
   - macOS: Built-in
3. Try: `avahi-browse -a` to see mDNS services

### Certificate Mismatch

If browser shows "hostname mismatch" error:
1. Check you're using the correct FQDN
2. Run `ssl clear` and reboot to regenerate certificate
3. Verify SAN: `openssl s_client -connect host:443 | openssl x509 -text | grep -A1 "Subject Alternative Name"`

### Router Shows Wrong Hostname

After firmware update, the router may cache old hostname. Either:
1. Reboot router
2. Wait for DHCP lease renewal
3. Release/renew DHCP on device (reboot device)

## Implementation Files

| File | Description |
|------|-------------|
| `include/modules/mdns_service.h` | mDNS service module |
| `include/security/cert_generator.h` | Certificate generation with SAN |
| `include/modules/wifi_provisioning.h` | DHCP hostname setting |
| `components/mdns/` | ESP-IDF mDNS component |

## Component Dependencies

The mDNS component is cloned from [esp-protocols](https://github.com/espressif/esp-protocols) into `components/mdns/`. This workaround is needed because PlatformIO's ESP-IDF component manager doesn't reliably fetch managed components.

To update:
```bash
cd components
rm -rf mdns
git clone --depth 1 https://github.com/espressif/esp-protocols.git
mv esp-protocols/components/mdns .
rm -rf esp-protocols
```
