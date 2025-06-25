#include "ksocket.h"

#define BUFFER_SIZE MSG_SIZE
#define EOF_MARKER "ENDOFFILE"  // Match sender's EOF marker


int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <local_ip> <local_port> <remote_ip> <remote_port> <output_file>\n", argv[0]);
        return 1;
    }
    sleep(1);
    const char *local_ip = argv[1];
    int local_port = atoi(argv[2]);
    const char *remote_ip = argv[3];
    int remote_port = atoi(argv[4]);
    const char *filename = argv[5];
    
    // Create output file
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("fopen");
        return 1;
    }
    
    // Create KTP socket
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        fclose(file);
        return 1;
    }
    
    printf("[user2] KTP socket created: %d\n", sockfd);
    
    // Set up local address
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(local_port);
    inet_pton(AF_INET, local_ip, &local_addr.sin_addr);
    
    // Set up remote address
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    inet_pton(AF_INET, remote_ip, &remote_addr.sin_addr);
    
    // Bind socket
    if (k_bind(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr),
               (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
        perror("k_bind");
        k_close(sockfd);
        fclose(file);
        return 1;
    }
    
    printf("[user2] KTP socket bound to local %s:%d and remote %s:%d\n", 
           local_ip, local_port, remote_ip, remote_port);
    
    // Receive file data
    char buffer[BUFFER_SIZE];
    ssize_t recv_bytes;
    size_t total_received = 0;
    
    printf("[user2] Waiting for data...\n");
    
    while (1) {
        recv_bytes = k_recvfrom(sockfd, buffer, BUFFER_SIZE, 0, NULL, NULL);
        // printf("norec\n");
        
        if (recv_bytes < 0) {
            if (errno == ENOMESSAGE) {
                // No message available, wait and retry
                // printf("nomes\n");
                usleep(100000);  // 100ms
                continue;
            } else {
                perror("k_recvfrom");
                break;
            }
        }
        // if(102400==total_received) printf("%s\n", buffer);
            // Check for EOF marker
        if (strncmp(buffer, EOF_MARKER, strlen(EOF_MARKER)) == 0) {
            printf("[user2] EOF received. Closing connection.\n");
            break;  // Exit loop
        }
        total_received += recv_bytes;
        printf("[user2] Received %zu bytes (total: %zu bytes)\n", 
               (size_t)recv_bytes, total_received);
        fflush(stdout);
        // Write received data to file
        size_t written = fwrite(buffer, 1, recv_bytes, file);
        if (written != (size_t)recv_bytes) {
            perror("fwrite");
            break;
        }    
        // Small delay to prevent CPU hogging
        usleep(10000);  // 10ms
    }
    
    printf("\n[user2] File transfer complete: %zu bytes received\n", total_received);
    
    // Close socket and file
    k_close(sockfd);
    fclose(file);
    
    return 0;
}