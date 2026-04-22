#!/bin/bash
#
# wifi-ap-setup.sh - Configure Raspberry Pi 5 as Wi-Fi Access Point
# 
# AuroreMkVII - Dual Network Architecture
# - eth0: IMU sensor data (UDP port 7070)
# - wlan0: Wi-Fi AP for remote operator control (aurore-link)
#
# Spec: AM7-L2-IF-008, ICD-009
# SSID: AuroreMkVII
# Password: aurore (WPA3-SAE, WPA2-PSK fallback)
# Band: 5 GHz, Channel 36
# Subnet: 192.168.4.0/24
#

set -e

echo "=== AuroreMkVII Wi-Fi AP Setup ==="
echo "This script configures the Pi 5 as a Wi-Fi access point."
echo ""

# Configuration
SSID="AuroreMkVII"
PASSWORD="aurore"
COUNTRY_CODE="US"
CHANNEL=36
FREQ_BAND=5
AP_IP="192.168.4.1"
NETMASK="255.255.255.0"
DHCP_RANGE_START=100
DHCP_RANGE_END=200
DHCP_LEASE_TIME="1h"

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo "Please run as root (sudo ./wifi-ap-setup.sh)"
    exit 1
fi

echo "Configuration:"
echo "  SSID: $SSID"
echo "  Password: $PASSWORD"
echo "  Country: $COUNTRY_CODE"
echo "  Channel: $CHANNEL (5 GHz)"
echo "  AP IP: $AP_IP"
echo "  DHCP Range: $AP_IP.$DHCP_RANGE_START - $AP_IP.$DHCP_RANGE_END"
echo ""

# Step 1: Update system packages
echo "[1/8] Updating system packages..."
apt update
apt upgrade -y

# Step 2: Install hostapd and dnsmasq
echo "[2/8] Installing hostapd and dnsmasq..."
apt install -y hostapd dnsmasq

# Step 3: Configure static IP for wlan0
echo "[3/8] Configuring static IP for wlan0..."
cat > /etc/dhcpcd.conf << EOF
# AuroreMkVII Wi-Fi AP Configuration
interface wlan0
    static ip_address=$AP_IP/24
    nohook wpa_supplicant
EOF

# Step 4: Enable IP forwarding
echo "[4/8] Enabling IP forwarding..."
sed -i '/#net.ipv4.ip_forward=1/c\#net.ipv4.ip_forward=1' /etc/sysctl.conf
echo "net.ipv4.ip_forward=1" >> /etc/sysctl.conf
sysctl -p

# Step 5: Configure hostapd (Wi-Fi AP)
echo "[5/8] Configuring hostapd..."
cat > /etc/hostapd/hostapd.conf << EOF
# AuroreMkVII Wi-Fi Access Point Configuration
# Spec: AM7-L2-IF-008, ICD-009

interface=wlan0
driver=nl80211
ssid=$SSID
hw_mode=a
channel=$CHANNEL
ieee80211d=1
country_code=$COUNTRY_CODE
ieee80211n=1
ieee80211ac=1
wmm_enabled=1

# WPA3-SAE with WPA2-PSK fallback
wpa=2
wpa_key_mgmt=WPA-PSK WPA-PSK-SAE
rsn_pairwise=CCMP
wpa_passphrase=$PASSWORD

# Beacon and DTIM settings
beacon_int=100
dtim_period=3

# Max clients
max_num_sta=4

# Disassociate low latency stations
disassoc_low_ack=1
EOF

# Configure hostapd to use our config
sed -i 's|#DAEMON_CONF=""|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' /etc/default/hostapd

# Step 6: Configure dnsmasq (DHCP server)
echo "[6/8] Configuring dnsmasq..."
mv /etc/dnsmasq.conf /etc/dnsmasq.conf.orig
cat > /etc/dnsmasq.conf << EOF
# AuroreMkVII DHCP Server Configuration
# Spec: AM7-L2-IF-008, ICD-009

interface=wlan0
bind-interfaces
server=8.8.8.8
server=8.8.4.4
domain-needed
bogus-priv

# DHCP range: 192.168.4.100 - 192.168.4.200, lease time 1 hour
dhcp-range=$AP_IP.$DHCP_RANGE_START,$AP_IP.$DHCP_RANGE_END,$NETMASK,$DHCP_LEASE_TIME

# DHCP options
dhcp-option=3,$AP_IP  # Gateway
dhcp-option=6,$AP_IP  # DNS server
dhcp-option=42,$AP_IP # NTP server

# Log DHCP requests
log-dhcp
EOF

# Step 7: Configure NAT (optional - allows internet sharing via eth0)
echo "[7/8] Configuring NAT (internet sharing via eth0)..."
iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE
iptables -A FORWARD -i eth0 -o wlan0 -m state --state RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i wlan0 -o eth0 -j ACCEPT

# Save iptables rules
apt install -y iptables-persistent << EOF
y
y
EOF

# Step 8: Start and enable services
echo "[8/8] Starting and enabling services..."
systemctl unmask hostapd
systemctl enable hostapd
systemctl enable dnsmasq

# Restart networking to apply wlan0 static IP
systemctl restart dhcpcd

# Start services
systemctl start hostapd
systemctl start dnsmasq

echo ""
echo "=== Wi-Fi AP Setup Complete ==="
echo ""
echo "Wi-Fi Access Point Details:"
echo "  SSID: $SSID"
echo "  Password: $PASSWORD"
echo "  Security: WPA3-SAE (WPA2-PSK fallback)"
echo "  Band: 5 GHz, Channel $CHANNEL"
echo "  Network: 192.168.4.0/24"
echo "  Gateway/DNS: $AP_IP"
echo "  DHCP Range: $AP_IP.$DHCP_RANGE_START - $AP_IP.$DHCP_RANGE_END"
echo ""
echo "To verify AP is running:"
echo "  systemctl status hostapd"
echo "  systemctl status dnsmasq"
echo "  iw dev wlan0 info"
echo ""
echo "To monitor connected clients:"
echo "  iw dev wlan0 station dump"
echo ""
echo "Laptop connection instructions:"
echo "  1. Connect to Wi-Fi network: $SSID"
echo "  2. Enter password: $PASSWORD"
echo "  3. Laptop will obtain IP via DHCP (192.168.4.x)"
echo "  4. Run aurore-link and connect to 192.168.4.1:9000 (telemetry), 192.168.4.1:9002 (commands)"
echo ""
