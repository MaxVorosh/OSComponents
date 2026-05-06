#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// TCP header structure (without options)
struct tcphdr_simple {
    u_int16_t th_sport;     // source port
    u_int16_t th_dport;     // destination port
    u_int32_t th_seq;       // sequence number
    u_int32_t th_ack;       // acknowledgement number
    u_int8_t th_x2:4;       // (unused)
    u_int8_t th_off:4;      // data offset
    u_int8_t th_flags;      // control flags
    u_int16_t th_win;       // window size
    u_int16_t th_sum;       // checksum
    u_int16_t th_urp;       // urgent pointer
};

// Pseudo header for checksum calculation
struct pseudo_header {
    u_int32_t source_address;
    u_int32_t dest_address;
    u_int8_t placeholder;
    u_int8_t protocol;
    u_int16_t tcp_length;
};

// Calculate checksum
unsigned short checksum(unsigned short *buffer, int length) {
    unsigned long sum = 0;
    unsigned short *buf = buffer;
    
    while (length > 1) {
        sum += *buf++;
        length -= 2;
    }
    
    if (length == 1) {
        sum += *(unsigned char *)buf;
    }
    
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    
    return (unsigned short)(~sum);
}

// Create TCP packet with specified flags
int send_tcp_packet(int sock, const char *src_ip, const char *dst_ip, 
                    int src_port, int dst_port, u_int32_t seq, u_int32_t ack,
                    u_int8_t flags, char *payload, int payload_len) {
    char datagram[4096];
    struct iphdr *iph = (struct iphdr *)datagram;
    struct tcphdr_simple *tcph = (struct tcphdr_simple *)(datagram + sizeof(struct iphdr));
    struct pseudo_header psh;
    struct sockaddr_in sin;
    int one = 1;
    
    memset(datagram, 0, 4096);
    
    // IP header
    iph->ihl = 5;
    iph->version = 4;
    iph->tos = 0;
    iph->tot_len = sizeof(struct iphdr) + sizeof(struct tcphdr_simple) + payload_len;
    iph->id = htons(rand() % 65535);
    iph->frag_off = 0;
    iph->ttl = 64;
    iph->protocol = IPPROTO_TCP;
    iph->check = 0;
    iph->saddr = inet_addr(src_ip);
    iph->daddr = inet_addr(dst_ip);
    
    // TCP header
    tcph->th_sport = htons(src_port);
    tcph->th_dport = htons(dst_port);
    tcph->th_seq = htonl(seq);
    tcph->th_ack = htonl(ack);
    tcph->th_x2 = 0;
    tcph->th_off = 5;  // TCP header length in 32-bit words
    tcph->th_flags = flags;
    tcph->th_win = htons(5840);
    tcph->th_sum = 0;
    tcph->th_urp = 0;
    
    // Put payload after TCP header if any
    if (payload && payload_len > 0) {
        memcpy(datagram + sizeof(struct iphdr) + sizeof(struct tcphdr_simple), 
               payload, payload_len);
    }
    
    // Pseudo header for checksum
    psh.source_address = inet_addr(src_ip);
    psh.dest_address = inet_addr(dst_ip);
    psh.placeholder = 0;
    psh.protocol = IPPROTO_TCP;
    psh.tcp_length = htons(sizeof(struct tcphdr_simple) + payload_len);
    
    int psize = sizeof(struct pseudo_header) + sizeof(struct tcphdr_simple) + payload_len;
    char *pseudogram = malloc(psize);
    
    memcpy(pseudogram, (char *)&psh, sizeof(struct pseudo_header));
    memcpy(pseudogram + sizeof(struct pseudo_header), tcph, 
           sizeof(struct tcphdr_simple) + payload_len);
    
    tcph->th_sum = checksum((unsigned short *)pseudogram, psize);
    free(pseudogram);
    
    iph->check = checksum((unsigned short *)datagram, iph->tot_len);
    
    sin.sin_family = AF_INET;
    sin.sin_port = htons(dst_port);
    sin.sin_addr.s_addr = inet_addr(dst_ip);
    
    if (setsockopt(sock, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("setsockopt");
        return -1;
    }
    
    if (sendto(sock, datagram, iph->tot_len, MSG_CONFIRM, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("sendto");
        return -1;
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    int sock;
    const char *target_ip;
    int target_port;
    int wait_seconds = 5;
    int i;
    
    // Check command line arguments
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <target_ip> <target_port> [wait_seconds]\n", argv[0]);
        fprintf(stderr, "Example: %s 192.168.1.1 80 5\n", argv[0]);
        exit(1);
    }
    
    target_ip = argv[1];
    target_port = atoi(argv[2]);
    
    if (argc >= 4) {
        wait_seconds = atoi(argv[3]);
    }
    
    // Create raw socket
    sock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (sock < 0) {
        perror("socket");
        fprintf(stderr, "Error: This program requires root privileges.\n");
        exit(1);
    }
    
    // Send 15 SYN packets
    printf("Sending 15 SYN packets...\n");
    for (i = 0; i < 15; i++) {
        int src_port = 8080;
        
        if (send_tcp_packet(sock, "0.0.0.0", target_ip, src_port, target_port, 
                           i, 0, TH_SYN, NULL, 0) == 0) {
            printf("  Sent SYN packet %d/15 (src_port=%d, seq=%u)\n", 
                   i + 1, src_port, i);
        } else {
            printf("  Failed to send SYN packet %d/15\n", i + 1);
        }
        
        usleep(100000); // 100ms delay between packets
    }
    printf("Finished sending 15 SYN packets.\n\n");
    
    // Wait
    printf("Waiting for %d seconds...\n", wait_seconds);
    sleep(wait_seconds);
    printf("Wait finished.\n\n");
    
    // Send 10 ACK packets
    printf("Sending 10 ACK packets...\n");
    for (i = 0; i < 10; i++) {
        int src_port = 8080;
        
        if (send_tcp_packet(sock, "0.0.0.0", target_ip, src_port, target_port, 
                           i, i, TH_ACK, NULL, 0) == 0) {
            printf("  Sent ACK packet %d/10 (src_port=%d, seq=%u, ack=%u)\n", 
                   i + 1, src_port, i, i);
        } else {
            printf("  Failed to send ACK packet %d/10\n", i + 1);
        }
        
        usleep(100000); // 100ms delay between packets
    }
    printf("Finished sending 10 ACK packets.\n");

    // Send 15 SYN packets
    printf("Sending 15 SYN packets...\n");
    for (i = 0; i < 15; i++) {
        int src_port = 8080;
        
        if (send_tcp_packet(sock, "0.0.0.0", target_ip, src_port, target_port, 
                           i, 0, TH_SYN, NULL, 0) == 0) {
            printf("  Sent SYN packet %d/15 (src_port=%d, seq=%u)\n", 
                   i + 1, src_port, i);
        } else {
            printf("  Failed to send SYN packet %d/15\n", i + 1);
        }
        
        usleep(100000); // 100ms delay between packets
    }
    printf("Finished sending 15 SYN packets.\n\n");

    close(sock);
    return 0;
}