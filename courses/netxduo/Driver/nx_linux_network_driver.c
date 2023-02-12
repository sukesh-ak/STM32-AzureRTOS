/**************************************************************************/
/*                                                                        */
/*       Copyright (c) Microsoft Corporation. All rights reserved.        */
/*                                                                        */
/*       This software is licensed under the Microsoft Software License   */
/*       Terms for Microsoft Azure RTOS. Full text of the license can be  */
/*       found in the LICENSE file at https://aka.ms/AzureRTOS_EULA       */
/*       and in the root directory of this software.                      */
/*                                                                        */
/**************************************************************************/

#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <linux/if_packet.h>
#include "nx_api.h"

#ifdef NX_ENABLE_PPPOE
#include "nx_pppoe_server.h"
#endif

/* Define the Link MTU. Note this is not the same as the IP MTU.  The Link MTU
   includes the addition of the Physical Network header (usually Ethernet). This
   should be larger than the IP instance MTU by the size of the physical header. */
#define NX_LINK_MTU                 1514
#define NX_MAX_PACKET_SIZE          1536

/* Define Ethernet address format.  This is prepended to the incoming IP
   and ARP/RARP messages.  The frame beginning is 14 bytes, but for speed
   purposes, we are going to assume there are 16 bytes free in front of the
   prepend pointer and that the prepend pointer is 32-bit aligned.

   Byte Offset     Size            Meaning

   0           6           Destination Ethernet Address
   6           6           Source Ethernet Address
   12          2           Ethernet Frame Type, where:

   0x0800 -> IP Datagram
   0x0806 -> ARP Request/Reply
   0x0835 -> RARP request reply

   42          18          Padding on ARP and RARP messages only.  */

#define NX_ETHERNET_IP              0x0800
#define NX_ETHERNET_ARP             0x0806
#define NX_ETHERNET_RARP            0x8035
#define NX_ETHERNET_IPV6            0x86DD
#define NX_ETHERNET_PPPOE_DISCOVERY 0x8863
#define NX_ETHERNET_PPPOE_SESSION   0x8864
#define NX_ETHERNET_SIZE            14
#define NX_ETHERNET_MAC_SIZE        6

/* For the linux ethernet driver, physical addresses are allocated starting
   at the preset value and then incremented before the next allocation.  */

ULONG              nx_linux_address_msw =  0x0011;
ULONG              nx_linux_address_lsw =  0x22334457;

static const CHAR *nx_linux_interface_name = NX_LINUX_INTERFACE_NAME;
static int         nx_linux_interface_index = 0;
static pthread_t   nx_linux_receive_thread;
static NX_IP      *nx_linux_default_ip;
static int         nx_linux_socket = -1;

/* Define the buffer to store data that will be used by linux socket. */
static UCHAR nx_linux_transmit_buffer[NX_MAX_PACKET_SIZE];
static UCHAR nx_linux_receive_buffer[NX_MAX_PACKET_SIZE];


/* Define driver prototypes.  */

UINT  _nx_linux_initialize(NX_IP *ip_ptr);
UINT  _nx_linux_send_packet(NX_PACKET *packet_ptr);
void *_nx_linux_receive_thread_entry(void *arg);
VOID  _nx_linux_network_driver_output(NX_PACKET *packet_ptr);
VOID  _nx_linux_network_driver(NX_IP_DRIVER *driver_req_ptr);

/* Define interface capability.  */

#ifdef NX_ENABLE_INTERFACE_CAPABILITY
#define NX_INTERFACE_CAPABILITY (NX_INTERFACE_CAPABILITY_IPV4_RX_CHECKSUM |   \
                                 NX_INTERFACE_CAPABILITY_TCP_RX_CHECKSUM |    \
                                 NX_INTERFACE_CAPABILITY_UDP_RX_CHECKSUM |    \
                                 NX_INTERFACE_CAPABILITY_ICMPV4_RX_CHECKSUM | \
                                 NX_INTERFACE_CAPABILITY_ICMPV6_RX_CHECKSUM)
#endif /* NX_ENABLE_INTERFACE_CAPABILITY */

VOID nx_linux_set_interface_name(const CHAR *interface_name)
{
    nx_linux_interface_name = interface_name;
    nx_linux_interface_index = if_nametoindex(interface_name);
}

UINT _nx_linux_send_packet(NX_PACKET *packet_ptr)
{
ULONG              size = 0;
UCHAR             *data;
struct sockaddr_ll to_address;

    /* Make sure the data length is less than MTU. */
    if (packet_ptr -> nx_packet_length > NX_MAX_PACKET_SIZE)
    {
        return NX_NOT_SUCCESSFUL;
    }

    /* Set data pointer to be transmitted.  */
#ifndef NX_DISABLE_PACKET_CHAIN
    if (packet_ptr -> nx_packet_next)
    {
        if (nx_packet_data_retrieve(packet_ptr, nx_linux_transmit_buffer, &size))
        {
            return NX_NOT_SUCCESSFUL;
        }
        data = nx_linux_transmit_buffer;
    }
    else
#endif /* NX_DISABLE_PACKET_CHAIN */
    {
        data = packet_ptr -> nx_packet_prepend_ptr;
        size = packet_ptr -> nx_packet_length;
    }

    /* Set destination address.  */
    to_address.sll_family = AF_PACKET;
    to_address.sll_protocol = htons(ETH_P_ALL);
    to_address.sll_ifindex = nx_linux_interface_index;

    if (sendto(nx_linux_socket, (CHAR *)data, size, 0, (struct sockaddr *)&to_address, sizeof(to_address)) != size)
    {
        return NX_NOT_SUCCESSFUL;
    }

    return NX_SUCCESS;
}

void *_nx_linux_receive_thread_entry(void *arg)
{
fd_set             read_fds;
UCHAR             *data;
int                bytes_received;
struct sockaddr_ll from_address;
int                address_len;
NX_PACKET         *packet_ptr;
UINT               status;
UINT               packet_type;

    /* Loop to capture packets. */
    for (;;)
    {
        FD_ZERO(&read_fds);
        FD_SET(nx_linux_socket, &read_fds);

        if (select(nx_linux_socket + 1, &read_fds, NULL, NULL, NULL) <= 0)
        {
            continue;
        }

        _tx_thread_context_save();

        status = nx_packet_allocate(nx_linux_default_ip -> nx_ip_default_packet_pool,
                                    &packet_ptr, NX_RECEIVE_PACKET, NX_NO_WAIT);

        if (status)
        {
            packet_ptr = NX_NULL;
            data = nx_linux_receive_buffer;
        }
        else if (nx_linux_default_ip -> nx_ip_default_packet_pool -> nx_packet_pool_payload_size >= (NX_LINK_MTU + 2))
        {
            data = packet_ptr -> nx_packet_prepend_ptr + 2;
        }
        else
        {
            data = nx_linux_receive_buffer;
        }

        address_len = sizeof(from_address);
        bytes_received = recvfrom(nx_linux_socket, (VOID *)data, NX_LINK_MTU, 0,
                                  (struct sockaddr *)&from_address, &address_len);

        if (bytes_received < 14)
        {

            /* Not an Ethernet header.  */
            if (packet_ptr)
            {
                nx_packet_release(packet_ptr);
            }
            _tx_thread_context_restore();
            continue;
        }

        if (packet_ptr == NX_NULL)
        {

            /* No packet available. Drop it and continue.  */
            _tx_thread_context_restore();
            continue;
        }

        /* Make sure IP header is 4-byte aligned. */
        packet_ptr -> nx_packet_prepend_ptr += 2;
        packet_ptr -> nx_packet_append_ptr += 2;

        if (data == nx_linux_receive_buffer)
        {

            /* Copy data into packet.  */

            status = nx_packet_data_append(packet_ptr, (VOID *)data, bytes_received,
                                           nx_linux_default_ip -> nx_ip_default_packet_pool, NX_NO_WAIT);
            if (status)
            {
                nx_packet_release(packet_ptr);
                _tx_thread_context_restore();
                continue;
            }
        }
        else
        {
            packet_ptr -> nx_packet_length = (ULONG)bytes_received;
            packet_ptr -> nx_packet_append_ptr += (ULONG)bytes_received;
        }

        /* Pickup the packet header to determine where the packet needs to be sent.  */
        packet_type =  (((UINT)(*(packet_ptr -> nx_packet_prepend_ptr + 12))) << 8) |
                        ((UINT)(*(packet_ptr -> nx_packet_prepend_ptr + 13)));

        /* Route the incoming packet according to its ethernet type.  */
        if ((packet_type == NX_ETHERNET_IP) || (packet_type == NX_ETHERNET_IPV6))
        {

            /* Note:  The length reported by some Ethernet hardware includes bytes after the packet
               as well as the Ethernet header.  In some cases, the actual packet length after the
               Ethernet header should be derived from the length in the IP header (lower 16 bits of
               the first 32-bit word).  */

            /* Clean off the Ethernet header.  */
            packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr + NX_ETHERNET_SIZE;

            /* Adjust the packet length.  */
            packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length - NX_ETHERNET_SIZE;

            _nx_ip_packet_deferred_receive(nx_linux_default_ip, packet_ptr);
        }
        else if (packet_type == NX_ETHERNET_ARP)
        {

            /* Clean off the Ethernet header.  */
            packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr + NX_ETHERNET_SIZE;

            /* Adjust the packet length.  */
            packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length - NX_ETHERNET_SIZE;

            _nx_arp_packet_deferred_receive(nx_linux_default_ip, packet_ptr);
        }
        else if (packet_type == NX_ETHERNET_RARP)
        {

            /* Clean off the Ethernet header.  */
            packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr + NX_ETHERNET_SIZE;

            /* Adjust the packet length.  */
            packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length - NX_ETHERNET_SIZE;

            _nx_rarp_packet_deferred_receive(nx_linux_default_ip, packet_ptr);
        }
#ifdef NX_ENABLE_PPPOE
        else if ((packet_type == NX_ETHERNET_PPPOE_DISCOVERY) ||
                 (packet_type == NX_ETHERNET_PPPOE_SESSION))
        {

            /* Clean off the Ethernet header.  */
            packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr + NX_ETHERNET_SIZE;

            /* Adjust the packet length.  */
            packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length - NX_ETHERNET_SIZE;

            /* Route to the PPPoE receive function.  */
            _nx_pppoe_packet_deferred_receive(packet_ptr);
        }
#endif
        else
        {

            /* Invalid ethernet header... release the packet.  */
            nx_packet_release(packet_ptr);
        }
        _tx_thread_context_restore();
    }
    return((void *)0);
}

UINT _nx_linux_initialize(NX_IP *ip_ptr)
{
struct sched_param sp;
struct sockaddr_ll sa;

    /* Define the thread's priority. */
#ifdef TX_LINUX_PRIORITY_ISR
    sp.sched_priority = TX_LINUX_PRIORITY_ISR;
#else
    sp.sched_priority = 2;
#endif

    /* Return if socket has been created. */
    if (nx_linux_socket > 0)
    {
        return(NX_ALREADY_ENABLED);
    }

    nx_linux_socket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (nx_linux_socket < 0)
    {
        return(NX_NOT_CREATED);
    }

    nx_linux_interface_index = if_nametoindex(nx_linux_interface_name);
    sa.sll_family = AF_PACKET;
    sa.sll_protocol = htons(ETH_P_ALL);
    sa.sll_ifindex = nx_linux_interface_index;
    if (bind(nx_linux_socket, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        close(nx_linux_socket);
        return(NX_NOT_BOUND);
    }

    nx_linux_default_ip = ip_ptr;

    /* Create a Linux thread to loop for capturing packets */
    pthread_create(&nx_linux_receive_thread, NULL, _nx_linux_receive_thread_entry, NULL);

    /* Set the thread's policy and priority */
    pthread_setschedparam(nx_linux_receive_thread, SCHED_FIFO, &sp);

    return NX_SUCCESS;
}


VOID  _nx_linux_network_driver_output(NX_PACKET *packet_ptr)
{
UINT old_threshold = 0;

    /* Disable preemption.  */
    tx_thread_preemption_change(tx_thread_identify(), 0, &old_threshold);

    _nx_linux_send_packet(packet_ptr);

    /* Remove the Ethernet header.  In real hardware environments, this is typically
       done after a transmit complete interrupt.  */
    packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr + NX_ETHERNET_SIZE;

    /* Adjust the packet length.  */
    packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length - NX_ETHERNET_SIZE;

    /* Now that the Ethernet frame has been removed, release the packet.  */
    nx_packet_transmit_release(packet_ptr);

    /* Restore preemption.  */
    tx_thread_preemption_change(tx_thread_identify(), old_threshold, &old_threshold);
}


VOID  _nx_linux_network_driver(NX_IP_DRIVER *driver_req_ptr)
{
NX_IP        *ip_ptr;
NX_PACKET    *packet_ptr;
ULONG        *ethernet_frame_ptr;
NX_INTERFACE *interface_ptr;
UINT          interface_index;

    /* Setup the IP pointer from the driver request.  */
    ip_ptr =  driver_req_ptr -> nx_ip_driver_ptr;

    /* Default to successful return.  */
    driver_req_ptr -> nx_ip_driver_status =  NX_SUCCESS;

    /* Setup interface pointer.  */
    interface_ptr = driver_req_ptr -> nx_ip_driver_interface;

    /* Obtain the index number of the network interface. */
    interface_index = interface_ptr -> nx_interface_index;

    /* Process according to the driver request type in the IP control
       block.  */
    switch (driver_req_ptr -> nx_ip_driver_command)
    {

        case NX_LINK_INTERFACE_ATTACH:
        {
            interface_ptr = (NX_INTERFACE *)(driver_req_ptr -> nx_ip_driver_interface);
            break;
        }

        case NX_LINK_INITIALIZE:
        {

            /* Device driver shall initialize the Ethernet Controller here. */

            /* Once the Ethernet controller is initialized, the driver needs to
            configure the NetX Interface Control block, as outlined below. */

            /* The nx_interface_ip_mtu_size should be the MTU for the IP payload.
            For regular Ethernet, the IP MTU is 1500. */
            nx_ip_interface_mtu_set(ip_ptr, interface_index, (NX_LINK_MTU - NX_ETHERNET_SIZE));

            /* Set the physical address (MAC address) of this IP instance.  */
            /* For this linux driver, the MAC address is constructed by
            incrementing a base lsw value, to simulate multiple nodes hanging on the
            ethernet.  */
            nx_ip_interface_physical_address_set(ip_ptr, interface_index,
                                                nx_linux_address_msw,
                                                nx_linux_address_lsw,
                                                NX_FALSE);

            /* Indicate to the IP software that IP to physical mapping is required.  */
            nx_ip_interface_address_mapping_configure(ip_ptr, interface_index, NX_TRUE);

            _nx_linux_initialize(ip_ptr);

#ifdef NX_ENABLE_INTERFACE_CAPABILITY
            nx_ip_interface_capability_set(ip_ptr, interface_index, NX_INTERFACE_CAPABILITY);
#endif /* NX_ENABLE_INTERFACE_CAPABILITY */
            break;
        }

        case NX_LINK_ENABLE:
        {

            /* Process driver link enable.  An Ethernet driver shall enable the
            transmit and reception logic.  Once the IP stack issues the
            LINK_ENABLE command, the stack may start transmitting IP packets. */


            /* In the driver, just set the enabled flag.  */
            interface_ptr -> nx_interface_link_up =  NX_TRUE;

            break;
        }

        case NX_LINK_DISABLE:
        {

            /* Process driver link disable.  This command indicates the IP layer
            is not going to transmit any IP datagrams, nor does it expect any
            IP datagrams from the interface.  Therefore after processing this command,
            the device driver shall not send any incoming packets to the IP
            layer.  Optionally the device driver may turn off the interface. */

            /* In the linux driver, just clear the enabled flag.  */
            interface_ptr -> nx_interface_link_up =  NX_FALSE;

            break;
        }

        case NX_LINK_PACKET_SEND:
        case NX_LINK_PACKET_BROADCAST:
        case NX_LINK_ARP_SEND:
        case NX_LINK_ARP_RESPONSE_SEND:
        case NX_LINK_RARP_SEND:
#ifdef NX_ENABLE_PPPOE
        case NX_LINK_PPPOE_DISCOVERY_SEND:
        case NX_LINK_PPPOE_SESSION_SEND:
#endif
        {

            /*
               The IP stack sends down a data packet for transmission.
               The device driver needs to prepend a MAC header, and fill in the
               Ethernet frame type (assuming Ethernet protocol for network transmission)
               based on the type of packet being transmitted.

               The following sequence illustrates this process.
             */

            /* Place the ethernet frame at the front of the packet.  */
            packet_ptr =  driver_req_ptr -> nx_ip_driver_packet;

            /* Adjust the prepend pointer.  */
            packet_ptr -> nx_packet_prepend_ptr =  packet_ptr -> nx_packet_prepend_ptr - NX_ETHERNET_SIZE;

            /* Adjust the packet length.  */
            packet_ptr -> nx_packet_length =  packet_ptr -> nx_packet_length + NX_ETHERNET_SIZE;

            /* Setup the ethernet frame pointer to build the ethernet frame.  Backup another 2
               bytes to get 32-bit word alignment.  */
            ethernet_frame_ptr =  (ULONG *)(packet_ptr -> nx_packet_prepend_ptr - 2);

            /* Build the ethernet frame.  */
            *ethernet_frame_ptr     =  driver_req_ptr -> nx_ip_driver_physical_address_msw;
            *(ethernet_frame_ptr + 1) =  driver_req_ptr -> nx_ip_driver_physical_address_lsw;
            *(ethernet_frame_ptr + 2) =  (interface_ptr -> nx_interface_physical_address_msw << 16) |
                (interface_ptr -> nx_interface_physical_address_lsw >> 16);
            *(ethernet_frame_ptr + 3) =  (interface_ptr -> nx_interface_physical_address_lsw << 16);

            if (driver_req_ptr -> nx_ip_driver_command == NX_LINK_ARP_SEND)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_ARP;
            }
            else if (driver_req_ptr -> nx_ip_driver_command == NX_LINK_ARP_RESPONSE_SEND)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_ARP;
            }
            else if (driver_req_ptr -> nx_ip_driver_command == NX_LINK_RARP_SEND)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_RARP;
            }
#ifdef NX_ENABLE_PPPOE
            else if (driver_req_ptr -> nx_ip_driver_command == NX_LINK_PPPOE_DISCOVERY_SEND)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_PPPOE_DISCOVERY;
            }
            else if (driver_req_ptr -> nx_ip_driver_command == NX_LINK_PPPOE_SESSION_SEND)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_PPPOE_SESSION;
            }
#endif
            else if (packet_ptr -> nx_packet_ip_version == 4)
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_IP;
            }
            else
            {
                *(ethernet_frame_ptr + 3) |= NX_ETHERNET_IPV6;
            }



            /* Endian swapping if NX_LITTLE_ENDIAN is defined.  */
            NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr));
            NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 1));
            NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 2));
            NX_CHANGE_ULONG_ENDIAN(*(ethernet_frame_ptr + 3));

            /* At this point, the packet is a complete Ethernet frame, ready to be transmitted.
               The driver shall call the actual Ethernet transmit routine and put the packet
               on the wire.

               In this example, the linux network transmit routine is called. */
            _nx_linux_network_driver_output(packet_ptr);
            break;
        }

        case NX_LINK_MULTICAST_JOIN:
        {

            /* The IP layer issues this command to join a multicast group.  Note that
            multicast operation is required for IPv6.

            On a typically Ethernet controller, the driver computes a hash value based
            on MAC address, and programs the hash table.

            It is likely the driver also needs to maintain an internal MAC address table.
            Later if a multicast address is removed, the driver needs
            to reprogram the hash table based on the remaining multicast MAC addresses. */

            break;
        }

        case NX_LINK_MULTICAST_LEAVE:
        {

            /* The IP layer issues this command to remove a multicast MAC address from the
            receiving list.  A device driver shall properly remove the multicast address
            from the hash table, so the hardware does not receive such traffic.  Note that
            in order to reprogram the hash table, the device driver may have to keep track of
            current active multicast MAC addresses. */

            /* The following procedure only applies to our linux network driver, which manages
            multicast MAC addresses by a simple look up table. */

            break;
        }

        case NX_LINK_GET_STATUS:
        {

            /* Return the link status in the supplied return pointer.  */
            *(driver_req_ptr -> nx_ip_driver_return_ptr) =  ip_ptr -> nx_ip_interface[0].nx_interface_link_up;
            break;
        }

        case NX_LINK_DEFERRED_PROCESSING:
        {

            /* Driver defined deferred processing. This is typically used to defer interrupt
            processing to the thread level.

            A typical use case of this command is:
            On receiving an Ethernet frame, the RX ISR does not process the received frame,
            but instead records such an event in its internal data structure, and issues
            a notification to the IP stack (the driver sends the notification to the IP
            helping thread by calling "_nx_ip_driver_deferred_processing()".  When the IP stack
            gets a notification of a pending driver deferred process, it calls the
            driver with the NX_LINK_DEFERRED_PROCESSING command.  The driver shall complete
            the pending receive process.
            */

            /* The linux driver doesn't require a deferred process so it breaks out of
            the switch case. */


            break;
        }

        case NX_LINK_SET_PHYSICAL_ADDRESS:
        {

            /* Set mac address.  */
            nx_linux_address_msw = driver_req_ptr -> nx_ip_driver_physical_address_msw;
            nx_linux_address_lsw = driver_req_ptr -> nx_ip_driver_physical_address_lsw;
            break;
        }

        default:
        {

            /* Invalid driver request.  */
            /* Return the unhandled command status.  */
            driver_req_ptr -> nx_ip_driver_status =  NX_UNHANDLED_COMMAND;
        }
    }
}

