#include "ksocket.h"
#include <sys/time.h>

// Declare shared memory and semaphore variables
int shmid;
// extern int sem_id;
// extern int sem_request;
// extern int sem_req_rel;
// extern struct sembuf pop, vop;

// Function to initialize shared memory and semaphores
void init_shared_memory() {
    key_t shm_key = SHM_KEY;
    key_t sem_key = SEM_KEY;

    // Create shared memory
    shmid = shmget(shm_key, N * sizeof(struct SM_entry), 0777 | IPC_CREAT);
    if (shmid == -1) {
        perror("shmget");
        exit(1);
    }
    printf("[init_shared_memory] Shared memory created with key: 0x%x, shmid: %d\n", shm_key, shmid);

    // Attach to shared memory
    SM = (struct SM_entry *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1) {
        perror("shmat");
        exit(1);
    }
    printf("[init_shared_memory] Shared memory attached at address: %p\n", SM);

    // Create semaphore
    sem_id = semget(sem_key, 1, 0777 | IPC_CREAT);
    if (sem_id == -1) {
        perror("semget");
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }
    // Create semaphores for request handling
    sem_request = semget(SEM_REQUEST_KEY, 1, 0777 | IPC_CREAT);
    sem_req_rel = semget(SEM_REQREL_KEY, 1, 0777 | IPC_CREAT);
    printf("[init_shared_memory] Semaphore created with key: 0x%x, semid: %d\n", sem_key, sem_id);

    // Initialize semaphore
    if (semctl(sem_id, 0, SETVAL, 1) == -1) {
        perror("semctl");
        exit(1);
    }
    // Initialize semaphores
    semctl(sem_request, 0, SETVAL, 0);  // Initially locked
    semctl(sem_req_rel, 0, SETVAL, 0);  // Initially locked
    // Initialize shared memory
    memset(SM, 0, N * sizeof(struct SM_entry));
    for (int i = 0; i < N; i++) {
        SM[i].is_free = 1;
    }
    printf("[init_shared_memory] Shared memory and semaphores initialized successfully.\n");
}

// receiver thread R
void* receiver_thread(void *arg) {
    fd_set readfds;

    while (1) {
        FD_ZERO(&readfds);
        int max_fd = 0;

        // lock sm
        P(sem_id);
        for (int i = 0; i < N; i++) {
            if (SM[i].is_free == 0) {
                FD_SET(SM[i].udp_sockfd, &readfds);
                if (SM[i].udp_sockfd > max_fd) {
                    max_fd = SM[i].udp_sockfd;
                }
            }
        }
        V(sem_id);
        struct timeval time;
        time.tv_sec = T;
        time.tv_usec = 0;

        // Call select on the updated fd_set.
        int ret = select(max_fd + 1, &readfds, NULL, NULL, &time);
        if (ret == -1) {
            perror("select() error");
            continue;  // or handle the error appropriately
        }
        if (ret == 0) {
            // Timeout case: process duplicate ACK sending for each active socket.
            P(sem_id);
            for (int i = 0; i < N; i++) {
                if (SM[i].is_free == 0) {
                    // If the receiver buffer was previously full and now has space,
                    // clear the nospace flag.
                    if (SM[i].nospace && SM[i].rwnd.size > 0) {
                        SM[i].nospace = 0;
                    }
                    // Compute last expected sequence.
                    // int lastseq = (SM[i].rwnd.start_seq + 255) % 256;

                    // Construct ACK message.
                    char ack[14];
                    ack[0] = '0';
                    for (int j = 0; j < 8; j++) {
                        ack[8 - j] = ((SM[i].last_ack_seq >> j) & 1) + '0';
                    }
                    for (int j = 0; j < 4; j++) {
                        ack[12 - j] = ((SM[i].rwnd.size >> j) & 1) + '0';
                    }
                    ack[13] = '\0';

                    int k = sendto(SM[i].udp_sockfd, ack, 14, 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr));
                    if (k < 0) {
                        perror("Error Sending ACK on timeout");
                    }
                    printf("sent dup ack with for sockfd %d seq %d\n", i, SM[i].last_ack_seq);
                    // printf("receiver dupack\n");
                }
            }
            V(sem_id);
        }
        else{
            // At least one fd is ready.
            P(sem_id);
            struct sockaddr_in peer_addr;
            socklen_t addr_len = sizeof(peer_addr);
            for (int i = 0; i < N; i++) {
                if (SM[i].is_free == 0 && FD_ISSET(SM[i].udp_sockfd, &readfds)) {
                    char buffer[532];

                    int n = recvfrom(SM[i].udp_sockfd, buffer, 532, 0, (struct sockaddr *)&peer_addr, &addr_len);
                    if (n < 0) {
                        perror("udprecv");
                    } else {
                        if (buffer[0] == '0') {
                            int seq = 0, rwnd = 0;
                            for (int j = 1; j <= 8; j++) {
                                seq = (seq << 1) | (buffer[j] - '0');
                            }
                            for (int j = 9; j <= 12; j++) {
                                rwnd = (rwnd << 1) | (buffer[j] - '0');
                            }

                            if (SM[i].swnd.seq_nums[seq] >= 0 || (seq < SM[i].swnd.base && seq + 256 > SM[i].swnd.base)) {
                                int j = SM[i].swnd.base;
                                while (j != (seq + 1) % 256) {
                                    SM[i].swnd.seq_nums[j] = -1;
                                    SM[i].timers[j] = -1;
                                    SM[i].send_count--;
                                    j = (j + 1) % 256;
                                }
                                SM[i].swnd.base = (seq + 1) % 256;
                                printf("Received ack with seq %d\n", seq);
                            }
                            else{
                                printf("Received dupack with seq %d\n", seq);
                            }
                            SM[i].swnd.size = rwnd;
                        } else {
                            int seq = 0, len_payload = 0;
                            for (int j = 1; j <= 8; j++) {
                                seq = (seq << 1) | (buffer[j] - '0');
                            }
                            if (dropMessage(p)) {
                                printf("[Thread R] Dropped message for socket %d and seq %d\n", i, seq);
                                continue;
                            }
                            for (int j = 9; j <= 18; j++) {
                                len_payload = (len_payload << 1) | (buffer[j] - '0');
                            }
                            if (seq == SM[i].rwnd.next_seq) {
                                // SM[i].rwnd.next_seq = (SM[i].rwnd.next_seq + 1) % 256;
                                int buff_ind = SM[i].rwnd.seq_nums[seq];
                                memcpy(SM[i].recv_buf[buff_ind], buffer + 19, len_payload);
                                SM[i].recv_flag[buff_ind] = 1;
                                
                                SM[i].recv_len[SM[i].rwnd.seq_nums[seq]] = len_payload;
                                while (SM[i].rwnd.seq_nums[SM[i].rwnd.next_seq] >= 0 &&
                                       SM[i].recv_flag[SM[i].rwnd.seq_nums[SM[i].rwnd.next_seq]] == 1){
                                    SM[i].last_ack_seq=SM[i].rwnd.next_seq;
                                    SM[i].rwnd.size--;
                                    SM[i].rwnd.next_seq = (SM[i].rwnd.next_seq + 1) % 256;
                                    }
                                if (SM[i].rwnd.size == 0) {
                                    SM[i].nospace = 1;
                                }
                                printf("Received inorder data msg with seq %d\n", seq);
                                // int next_seq = (SM[i].rwnd.start_seq + 15) % 256;
                                char ack[14];
                                ack[0] = '0';
                                for (int j = 0; j < 8; j++) {
                                    ack[8 - j] = ((SM[i].last_ack_seq >> j) & 1) + '0';
                                }
                                for (int j = 0; j < 4; j++) {
                                    ack[12 - j] = ((SM[i].rwnd.size >> j) & 1) + '0';
                                }
                                ack[13] = '\0';
                                int k = sendto(SM[i].udp_sockfd, ack, 14, 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr));
                                if (k < 0) {
                                    perror("Error Sending ACK for data message");
                                }
                                printf("Sent cumulative ack with seq %d\n", SM[i].last_ack_seq);
                            } else {
                                if (SM[i].rwnd.seq_nums[seq] >= 0 &&
                                    SM[i].recv_flag[SM[i].rwnd.seq_nums[seq]] == 0) {
                                    int buff_ind = SM[i].rwnd.seq_nums[seq];
                                    memcpy(SM[i].recv_buf[buff_ind], buffer + 19, len_payload);
                                    SM[i].recv_flag[buff_ind] = 1;
                                    // SM[i].rwnd.size--;
                                    SM[i].recv_len[SM[i].rwnd.seq_nums[seq]] = len_payload;
                                    printf("Received out of order data msg with seq %d\n", seq);
                                }
                                else{
                                    // printf("Received invalid data msg with seq %d\n", seq);
                                }
                            }
                        }
                    }
                }
                else if(SM[i].is_free == 0 && SM[i].nospace && SM[i].rwnd.size > 0){
                    // If the receiver buffer was previously full and now has space,
                    // clear the nospace flag.
                        SM[i].nospace = 0;

                    // Construct ACK message.
                    char ack[14];
                    ack[0] = '0';
                    for (int j = 0; j < 8; j++) {
                        ack[8 - j] = ((SM[i].last_ack_seq >> j) & 1) + '0';
                    }
                    for (int j = 0; j < 4; j++) {
                        ack[12 - j] = ((SM[i].rwnd.size >> j) & 1) + '0';
                    }
                    ack[13] = '\0';

                    int k = sendto(SM[i].udp_sockfd, ack, 14, 0, (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr));
                    if (k < 0) {
                        perror("Error Sending ACK on timeout");
                    }
                    printf("sent dup ack for sockfd %d with seq %d\n", i, SM[i].last_ack_seq);
                    // printf("receiver dupack\n");
                }
            }
            V(sem_id);
        }
    }



}

// sender thread S
void* sender_thread(void *arg) {
    while (1) {

        sleep(T/2);

        P(sem_id);
        time_t current_time = time(NULL);
        
        for (int i = 0; i < N; i++) {
            if (SM[i].is_free) continue;
            // printf("hello\n");
            // Check for timeouts and retransmit
            int timeout=0;
            if (SM[i].timers[SM[i].swnd.base] != -1 && time(NULL) - SM[i].timers[SM[i].swnd.base] > T) 
            {
                timeout = 1;
            }
            if(timeout){
                for(int seq= SM[i].swnd.base; seq!=SM[i].swnd.next_seq; seq=(seq+1)%(MAX_SEQ + 1)){
                    int buf_idx = SM[i].swnd.seq_nums[seq];
                    if (buf_idx == -1) continue;
                    // Build packet
                    char packet[19 + SM[i].send_len[buf_idx]];
                    packet[0] = '1'; // DATA type

                    // Encode sequence number (8 bits)
                    for (int k = 0; k < 8; k++)
                        packet[8 - k] = ((seq >> k) & 1) + '0';

                    // Encode length (10 bits)
                    int len = SM[i].send_len[buf_idx];
                    for (int k = 0; k < 10; k++)
                        packet[18 - k] = ((len >> k) & 1) + '0';

                    memcpy(packet + 19, SM[i].send_buf[buf_idx], len);

                    int sent=sendto(SM[i].udp_sockfd, packet, 19 + len, 0,
                           (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr));
                    if(sent<0) perror("Sending err\n");
                    SM[i].total_transmissions++;
                    SM[i].timers[seq] = current_time;
                    printf("[Thread S] Retransmitted seq=%d\n", seq);
                }
            }
            else{
                for(int seq= SM[i].swnd.base; seq!=SM[i].swnd.next_seq; seq=(seq+1)%(MAX_SEQ + 1)){
                    int buf_idx = SM[i].swnd.seq_nums[seq];
                    if (buf_idx == -1 || SM[i].timers[seq]!=-1) continue;
                    // build packet
                    char packet[19 + SM[i].send_len[buf_idx]];
                    packet[0] = '1'; // DATA type

                    // encode sequence number (8 bits)
                    for (int k = 0; k < 8; k++)
                        packet[8 - k] = ((seq >> k) & 1) + '0';

                    // encode length (10 bits)
                    int len = SM[i].send_len[buf_idx];
                    for (int k = 0; k < 10; k++)
                        packet[18 - k] = ((len >> k) & 1) + '0';

                    memcpy(packet + 19, SM[i].send_buf[buf_idx], len);

                    int sent=sendto(SM[i].udp_sockfd, packet, 19 + len, 0,
                           (struct sockaddr *)&SM[i].dest_addr, sizeof(SM[i].dest_addr));
                    if(sent<0) perror("Sending err\n");
                    SM[i].total_transmissions++;
                    SM[i].timers[seq] = current_time;
                    printf("[Thread S] transmitted seq=%d\n", seq);
                }
            }        
        }
        
        V(sem_id);
    }
    
    return NULL;
}

// garbage collector thread
void* gc_thread(void *arg) {
    while (1) {
        sleep(T);
        P(sem_id);
        for (int i = 0; i < N; i++) {
            if (SM[i].is_free) continue;

            if (kill(SM[i].process_id, 0) != 0) {
                printf("[GC Thread] Cleaning socket %d (PID %d)\n", i, SM[i].process_id);
                // close(SM[i].udp_sockfd);
                SM[i].udp_sockfd = -1;
                SM[i].is_free = 1;
            }
        }
        V(sem_id);
    }
    return NULL;
}

void cleanup(int signum) {
    printf("\n[initksocket] Cleaning up shared memory and semaphores...\n");
    
    // Detach from shared memory
    if (SM != NULL)
        shmdt(SM);
    
    // Remove shared memory
    if (shmid >= 0)
        shmctl(shmid, IPC_RMID, NULL);
    
    // Remove semaphore
    if (sem_id >= 0)
        semctl(sem_id, 0, IPC_RMID);
    semctl(sem_request, 0, IPC_RMID);
    semctl(sem_req_rel, 0, IPC_RMID);
    exit(0);
}

int main() {
    // Ignore SIGINT so we can clean up properly
    float P=p;
    printf("%f\n", P);
    signal(SIGINT, cleanup);
    signal(SIGTERM, cleanup);
    
    printf("[initksocket] Starting KTP socket initialization...\n");
    
    // Initialize shared memory and semaphores
    init_shared_memory();
    
    // Create threads
    pthread_t R, S, garbage_collector;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    printf("[initksocket] Creating receiver thread (Thread R)...\n");
    if (pthread_create(&R, &attr, receiver_thread, NULL) != 0) {
        perror("Failed to create receiver thread");
        cleanup(0);
        return 1;
    }
    
    printf("[initksocket] Creating sender thread (Thread S)...\n");
    if (pthread_create(&S, &attr, sender_thread, NULL) != 0) {
        perror("Failed to create sender thread");
        cleanup(0);
        return 1;
    }
    
    printf("[initksocket] Creating garbage collector thread...\n");
    if (pthread_create(&garbage_collector, &attr, gc_thread, NULL) != 0) {
        perror("Failed to create garbage collector thread");
        cleanup(0);
        return 1;
    }
        
    printf("[initksocket] KTP socket initialization complete. Running in background...\n");
    while (1) {
        // Wait for a request from user processes
        printf("Waiting for requests...\n");
        P(sem_request);  // P(sem_request)
        printf("Request received, processing...\n");
        
        P(sem_id);  // Lock shared memory
        printf("Shared memory locked, checking requests...\n");

        for (int i = 0; i < N; i++) {
            if (SM[i].is_free) continue;
            
            // Handle socket creation request
            if (SM[i].socket_request) {
                SM[i].udp_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                SM[i].socket_request = 0;  // Reset flag
                printf("[Thread S] Created socket for KTP entry %d\n", i);
            }

            // Handle bind request
            if (SM[i].bind_request) {
                if (bind(SM[i].udp_sockfd, (struct sockaddr*)&SM[i].src_addr, sizeof(SM[i].src_addr)) < 0) {
                    perror("bind");
                    SM[i].udp_sockfd = -1;
                }
                SM[i].bind_request = 0;  // Reset flag
                printf("[Thread S] Bound socket for KTP entry %d\n", i);
            }
        }

        V(sem_id);  // Release shared memory
        V(sem_req_rel);  // V(sem_req_rel)
    }
    // Wait for threads to complete here, which they never will unless interrupted
    pthread_join(R, NULL);
    pthread_join(S, NULL);
    pthread_join(garbage_collector, NULL);
    
    return 0;
}

