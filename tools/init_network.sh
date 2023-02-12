#! /bin/bash

ip link add veth1 type veth peer name veth2
ifconfig veth2 192.168.1.1 netmask 255.255.255.0 up
ifconfig veth1 up
ethtool --offload veth2 tx off
ethtool --offload veth1 tx off

sh -c "cat > /etc/dhcp/dhcpd.conf" <<EOT
subnet 192.168.1.0 netmask 255.255.255.0 {
    range 192.168.1.100 192.168.1.200;
    option routers 192.168.1.1;
    option domain-name-servers `host -v localhost| awk -F "[ #]" '/Received /{print$5}' | uniq`;
    option broadcast-address 192.168.1.255;
    default-lease-time 600;
    max-lease-time 7200;
}
EOT
sh -c "cat > /etc/default/isc-dhcp-server" <<EOT
INTERFACESv4="veth2"
EOT
pkill dhcpd
touch /dhcpd.leases
/usr/sbin/dhcpd -lf /dhcpd.leases

# sysctl net.ipv4.ip_forward=1
iptables -F
iptables -t nat -F
iptables -t nat -A POSTROUTING -s 192.168.1.0/24 -o eth0 -j MASQUERADE
iptables -A FORWARD -d 192.168.1.0/24 -o veth2 -j ACCEPT
iptables -A FORWARD -s 192.168.1.0/24 -j ACCEPT

#Debug
#ip link
#ip addr
#iptables -t nat -L -n
#cat /etc/dhcp/dhcpd.conf
#cat /etc/default/isc-dhcp-server
