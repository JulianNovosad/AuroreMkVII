# Wi-Fi Access Point Setup

**Module:** `aurore::wifi-ap-setup.sh`
**Spec:** AM7-L2-IF-008, ICD-009
**Purpose:** Configure Raspberry Pi 5 as Wi-Fi access point for remote operator control

---

## Overview

The AuroreMkVII system operates in a **dual network architecture**:
- **eth0**: IMU sensor data from Android phone (SensaGram app, UDP port 7070)
- **wlan0**: Wi-Fi access point for remote operator control (aurore-link laptop)

This script configures the Pi 5 as a Wi-Fi access point with:
- SSID: `AuroreMkVII`
- Password: `aurore` (WPA3-SAE with WPA2-PSK fallback)
- Band: 5 GHz (802.11ac), channel 36
- Subnet: 192.168.4.0/24
- DHCP: 192.168.4.100-200

---

## Quick Start

### Run the Setup Script

```bash
cd /home/pi/AuroreMkVII
sudo ./scripts/wifi-ap-setup.sh
```

The script will:
1. Update system packages
2. Install hostapd and dnsmasq
3. Configure static IP for wlan0 (192.168.4.1)
4. Enable IP forwarding (NAT)
5. Configure hostapd (Wi-Fi AP daemon)
6. Configure dnsmasq (DHCP server)
7. Set up iptables NAT rules
8. Start and enable services

### Verify AP is Running

```bash
# Check hostapd status
systemctl status hostapd

# Check dnsmasq status
systemctl status dnsmasq

# Check wlan0 configuration
iw dev wlan0 info

# View connected clients
iw dev wlan0 station dump
```

---

## Connection Instructions (Laptop)

1. **Scan for Wi-Fi networks**
   - Look for SSID: `AuroreMkVII`

2. **Connect with password**
   - Password: `aurore`
   - Security: WPA3-SAE (or WPA2-PSK if WPA3 not supported)

3. **Verify connection**
   ```bash
   # Should obtain IP in 192.168.4.x range
   ip addr show wlan0
   
   # Test connectivity to Pi 5
   ping 192.168.4.1
   ```

4. **Run aurore-link**
   ```bash
   cd aurore-link
   npm start
   # Connect to 192.168.4.1:9000 (telemetry), 192.168.4.1:9002 (commands)
   ```

---

## Configuration Details

### hostapd Configuration (`/etc/hostapd/hostapd.conf`)

```ini
interface=wlan0
driver=nl80211
ssid=AuroreMkVII
hw_mode=a              # 5 GHz band
channel=36             # 5 GHz channel 36
ieee80211n=1           # 802.11n (HT)
ieee80211ac=1          # 802.11ac (VHT)
wmm_enabled=1          # WMM for QoS

# WPA3-SAE with WPA2-PSK fallback
wpa=2
wpa_key_mgmt=WPA-PSK WPA-PSK-SAE
rsn_pairwise=CCMP
wpa_passphrase=aurore

# Beacon and DTIM
beacon_int=100         # 100 TU = 102.4 ms
dtim_period=3

# Max clients
max_num_sta=4
```

### dnsmasq Configuration (`/etc/dnsmasq.conf`)

```ini
interface=wlan0
bind-interfaces
server=8.8.8.8         # Upstream DNS
server=8.8.4.4

# DHCP range: 192.168.4.100-200, 1 hour lease
dhcp-range=192.168.4.100,192.168.4.200,255.255.255.0,1h

# Gateway and DNS
dhcp-option=3,192.168.4.1
dhcp-option=6,192.168.4.1
```

### Network Configuration (`/etc/dhcpcd.conf`)

```ini
interface wlan0
    static ip_address=192.168.4.1/24
    nohook wpa_supplicant
```

---

## Troubleshooting

### AP Not Visible

```bash
# Check hostapd is running
systemctl status hostapd

# Check for errors in journal
journalctl -u hostapd -f

# Verify wlan0 interface exists
iw dev
```

### Clients Can't Connect

```bash
# Check dnsmasq is running
systemctl status dnsmasq

# Check DHCP leases
cat /var/lib/misc/dnsmasq.leases

# Check firewall rules
iptables -t nat -L -n -v
```

### Poor Performance

1. **Change channel** (avoid crowded channels):
   ```bash
   # Scan for 5 GHz channels
   sudo iw dev wlan0 scan | grep -E "SSID|channel:"
   
   # Edit /etc/hostapd/hostapd.conf
   channel=48  # or another clear channel
   ```

2. **Reduce max clients** if too many devices:
   ```ini
   max_num_sta=2  # Reduce from 4 to 2
   ```

### NAT/Internet Sharing Not Working

```bash
# Verify IP forwarding is enabled
cat /proc/sys/net/ipv4/ip_forward  # Should be 1

# Check iptables rules
iptables -t nat -L POSTROUTING -n -v

# Restart iptables-persistent
systemctl restart netfilter-persistent
```

---

## Manual Configuration (Without Script)

### Install Dependencies

```bash
sudo apt update
sudo apt install -y hostapd dnsmasq iptables-persistent
```

### Configure hostapd

```bash
sudo nano /etc/hostapd/hostapd.conf
# Add configuration from above

sudo nano /etc/default/hostapd
# Set: DAEMON_CONF="/etc/hostapd/hostapd.conf"
```

### Configure dnsmasq

```bash
sudo mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
sudo nano /etc/dnsmasq.conf
# Add configuration from above
```

### Configure static IP

```bash
sudo nano /etc/dhcpcd.conf
# Add wlan0 static IP configuration
```

### Enable services

```bash
sudo systemctl unmask hostapd
sudo systemctl enable hostapd
sudo systemctl enable dnsmasq
sudo systemctl start hostapd
sudo systemctl start dnsmasq
```

---

## Security Considerations

### Current Configuration (Personal/Educational Use)

- **WPA3-SAE** with pre-shared key (password: `aurore`)
- **WPA2-PSK fallback** for compatibility
- **No client isolation** (clients can communicate with each other)
- **NAT enabled** (optional internet sharing via eth0)

### Hardening Options (If Needed)

1. **Change default password**:
   ```bash
   # Edit /etc/hostapd/hostapd.conf
   wpa_passphrase=YourStrongPassword123
   ```

2. **Enable client isolation** (prevent client-to-client communication):
   ```ini
   ap_isolate=1
   ```

3. **MAC address filtering**:
   ```ini
   macaddr_acl=1
   accept_mac_file=/etc/hostapd/mac.accept
   ```

4. **Disable WPA2 fallback** (WPA3-only, may break compatibility):
   ```ini
   wpa_key_mgmt=WPA-PSK-SAE
   ```

---

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Max throughput | ~200 Mbps | 802.11ac, 80 MHz channel |
| Typical latency | 2-5 ms | Laptop → Pi 5 ping |
| Max clients | 4 | Configurable via `max_num_sta` |
| DHCP lease time | 1 hour | Configurable |
| Beacon interval | 102.4 ms | Standard value |

---

## Related Documentation

- [spec.md](../spec.md) - AM7-L2-IF-008, ICD-009
- [imu_sensor.md](./imu_sensor.md) - IMU data integration (eth0)
- [telemetry.md](./telemetry.md) - Remote telemetry over Wi-Fi
