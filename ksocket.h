#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/select.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

// Socket type
#define SOCK_KTP 3

// Configuration parameters
#define T 5
#define p 0.05
#define N 5
#define MSG_SIZE 512
#define MAX_SEQ 255  // 8-bit sequence number

// IPC keys
#define SHM_KEY 0x1234  // Key for shared memory
#define SEM_KEY 0x5678  // Key for semaphores
#define SEM_REQUEST_KEY 0x9ABC  // Key for request semaphore
#define SEM_REQREL_KEY 0xDEF0   // Key for request-release semaphore
// Error codes
#define ENOSPACE 1001   // No space available
#define ENOTBOUND 1002  // Socket not bound
#define ENOMESSAGE 1003 // No message available

// Semaphore macros
#define P(s) semop(s, &pop, 1)
#define V(s) semop(s, &vop, 1)

// Message types
// typedef enum {
//     DATA,
//     ACK
// } msg_type;

// // KTP header structure
// struct ktp_header {
//     unsigned char seq;   // Sequence number
//     unsigned char type;  // Message type
//     unsigned char rwnd;  // Receive window size
//     unsigned char pad;   // Padding for alignment
// };

// Window structure
struct swindow {
    int base;      // Base sequence number
    int next_seq;  // start sequence number to be sent
    int size;      // Window size
    int seq_nums[MAX_SEQ + 1]; // Sequence numbers in the window
};

struct rwindow {
    int base;      // Base sequence number
    int next_seq;  // start sequence number to be sent
    int size;      // Window size
    int seq_nums[MAX_SEQ + 1]; // Sequence numbers in the window
};

// Shared memory entry structure
struct SM_entry {
    int is_free;              // Is this entry free?
    pid_t process_id;         // Process ID
    int udp_sockfd;           // UDP socket FD
    struct sockaddr_in src_addr;   // Source address
    struct sockaddr_in dest_addr;  // Destination address
    
    // Send Buffer
    char send_buf[10][MSG_SIZE];  // Buffer for outgoing messages
    int send_len[10];            // Length of each outgoing message
    int send_head;               // Index of the next message to send
    int send_tail;               // Index where new messages are added
    int send_count;              // count in send buffer
    
    // Receive Buffer
    char recv_buf[10][MSG_SIZE];  // Buffer for incoming messages
    int recv_len[10];            // Length of each incoming message
    int recv_flag[10];
    int recv_head;               // Index of the next message to read
    int recv_tail;               // Index where new messages are added
    int recv_count;              // Number of messages in the receive buffer
    
    int socket_request;    // Flag to request socket creation
    int bind_request;      // Flag to request binding
    struct swindow swnd;          // Send window
    struct rwindow rwnd;          // Receive window
    time_t timers[MAX_SEQ + 1];  // Timers for each sequence number
    int nospace;                 // Flag to indicate no space in receive buffer
    int last_ack_seq;            // Last acknowledged sequence number
    int total_transmissions;
};

// External variables
extern struct SM_entry *SM;
extern int sem_id;
extern int sem_request;
extern int sem_req_rel;
extern struct sembuf pop, vop;

// Function prototypes
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const struct sockaddr *src_addr, socklen_t src_addrlen,
          const struct sockaddr *dest_addr, socklen_t dest_addrlen);
ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float pr);

#endif  // KSOCKET_H


/*
make clean
make
./initksocket
./user2 127.0.0.1 5001 127.0.0.1 5002 output.txt
./user1 127.0.0.1 5002 127.0.0.1 5001 input.txt
*/