#include "ksocket.h"
#include <fcntl.h>
#include <sys/stat.h>

#define BUFFER_SIZE MSG_SIZE
#define EOF_MARKER "ENDOFFILE"  // Match sender's EOF marker
extern size_t tt;

int main(int argc, char *argv[]) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <local_ip> <local_port> <remote_ip> <remote_port> <file_to_send>\n", argv[0]);
        return 1;
    }
    
    const char *local_ip = argv[1];
    int local_port = atoi(argv[2]);
    const char *remote_ip = argv[3];
    int remote_port = atoi(argv[4]);
    const char *filename = argv[5];
    
    // Open file
    int file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        perror("open");
        return 1;
    }
    
    // Get file size
    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        perror("stat");
        close(file_fd);
        return 1;
    }
    
    off_t file_size = st.st_size;
    printf("[user1] File size: %ld bytes\n", (long)file_size);
    
    // Create KTP socket
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        close(file_fd);
        return 1;
    }
    
    printf("[user1] KTP socket created: %d\n", sockfd);
    
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
        close(file_fd);
        return 1;
    }
    
    printf("[user1] KTP socket bound to local %s:%d and remote %s:%d\n", 
           local_ip, local_port, remote_ip, remote_port);
    
    // Send file data
    char buffer[BUFFER_SIZE];
    ssize_t read_bytes, sent_bytes;
    size_t total_sent = 0;
    size_t original_transmissions = 0;
    
    printf("[user1] Starting file transfer...\n");
    
    while ((read_bytes = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
        // Try to send until successful
        while (1) {
            sent_bytes = k_sendto(sockfd, buffer, read_bytes, 0, 
                                 (struct sockaddr *)&remote_addr, sizeof(remote_addr));
            
            if (sent_bytes < 0) {
                if (errno == ENOSPACE) {
                    // Buffer full, wait and retry
                    printf("[user1] Send buffer full, waiting for 1 sec...\n");
                    sleep(1);  // 1s
                    continue;
                } else {
                    perror("k_sendto");
                    k_close(sockfd);
                    close(file_fd);
                    return 1;
                }
            }
            
            original_transmissions++;
            break;
        }
        
        total_sent += sent_bytes;
        float progress = (float)total_sent / file_size * 100;
        printf("[user1] Progress: %.2f%% (%zu/%ld bytes)\r", 
               progress, total_sent, (long)file_size);
        fflush(stdout);
    }
    memset(buffer, 0, BUFFER_SIZE);
    // Copy EOF_MARKER into the buffer and ensure null termination
    strncpy(buffer, EOF_MARKER, BUFFER_SIZE - 1);
    buffer[BUFFER_SIZE - 1] = '\0';  // Ensure null termination
    while(1){
        sent_bytes=k_sendto(sockfd, buffer, BUFFER_SIZE, 0, 
                (struct sockaddr *)&remote_addr, sizeof(remote_addr));
        if (sent_bytes < 0) {
            if (errno == ENOSPACE) {
                // Buffer full, wait and retry
                printf("[user1] Send buffer full, waiting...\n");
                usleep(500000);  // 500ms
                continue;
            } else {
                perror("k_sendto");
                k_close(sockfd);
                close(file_fd);
                return 1;
            }
        }
        break;
    }
    original_transmissions++;  // Count EOF as a transmission
    
    // Close socket and file
    k_close(sockfd);
    printf("\n[user1] File transfer complete: %zu bytes sent\n", total_sent);
    printf("[user1] Original transmissions: %zu\n", original_transmissions);
    printf("[user1] Total transmissions: %zu\n", tt);
    printf("[user1] Average transmissions per message: %.2f\n", 
           (float)tt / (float)original_transmissions);
    close(file_fd);
    
    return 0;
}