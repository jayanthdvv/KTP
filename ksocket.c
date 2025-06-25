#include "ksocket.h"

// Define shared memory and semaphore variables
struct SM_entry *SM;
// int shmid;
int sem_id;
int sem_request, sem_req_rel;
struct sembuf pop = {0, -1, 0}, vop = {0, 1, 0};
size_t tt;


// Function to attach to existing shared memory and semaphores
void attach_shared_memory() {
    key_t shm_key = SHM_KEY;
    key_t sem_key = SEM_KEY;


    int shmid = shmget(shm_key, N * sizeof(struct SM_entry), 0777);
    if (shmid == -1) {
        perror("shmget (attach)");
        exit(1);
    }
    // printf("[attach_shared_memory] Attached to shared memory with key: 0x%x, shmid: %d\n", shm_key, shmid);

    SM = (struct SM_entry *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1) {
        perror("shmat (attach)");
        exit(1);
    }
    // printf("[attach_shared_memory] Shared memory attached at address: %p\n", SM);

    sem_id = semget(sem_key, 1, 0777);
    if (sem_id == -1) {
        perror("semget (attach)");
        exit(1);
    }
    sem_request = semget(SEM_REQUEST_KEY, 1, 0777);
    sem_req_rel = semget(SEM_REQREL_KEY, 1, 0777);
    if(sem_request ==-1 || sem_req_rel == -1)
    {
        perror("semget_req (attach)");
        exit(1);
    }
    // printf("[attach_shared_memory] Attached to semaphore with key: 0x%x, semid: %d\n", sem_key, sem_id);
}

int k_socket(int domain, int type, int protocol) {
    // static int initialized = 0;

    if (type != SOCK_KTP) {
        errno = EINVAL;
        return -1;
    }

    // if (!initialized) {
        // Check if this is the init process or a user process
        attach_shared_memory();
        // initialized = 1;
    // }

    P(sem_id);
    for (int i = 0; i < N; i++) {
        if (!SM[i].is_free) continue;

        SM[i].is_free = 0;
        SM[i].socket_request = 1;  // set request flag
        SM[i].udp_sockfd = -1;
        // signal request and release shared memory
        V(sem_request);  
        V(sem_id);       

        // wait for socket to finish

        // block until initksocket handles request
        P(sem_req_rel); 
        // lock shared memory now
        P(sem_id);
        if (SM[i].udp_sockfd < 0) {
            SM[i].is_free = 1;
            perror("socket");
            V(sem_id);
            shmdt(SM);
            return -1;
        }
        

        for (int j = 0; j < 256; j++) 
        {
            SM[i].swnd.seq_nums[j] = -1;
            SM[i].timers[j] = -1;
            SM[i].rwnd.seq_nums[j] = -1;
        }
        for(int j=0;j<10;j++){
            SM[i].rwnd.seq_nums[j+1] = j;
        }
        SM[i].swnd.base = 1;        
        SM[i].swnd.next_seq = 1;
        SM[i].swnd.size = 10;      
        SM[i].rwnd.base = 1;
        SM[i].rwnd.next_seq = 1;
        SM[i].rwnd.size = 10;
        SM[i].process_id = getpid();
        SM[i].send_count = 0;
        SM[i].recv_count = 0;
        SM[i].total_transmissions=0;
        for(int j=0;j<10;j++){
            SM[i].recv_flag[j] = 0;
        }
        SM[i].send_head = SM[i].send_tail = 0;
        SM[i].recv_head = SM[i].recv_tail = 0;
        SM[i].nospace = 0;
        SM[i].last_ack_seq = 0;

        printf("[k_socket] Socket %d created successfully.\n", i);
        V(sem_id);
        shmdt(SM);
        return i;
    }

    V(sem_id);
    shmdt(SM);
    errno = ENOSPACE;
    return -1;
}

int k_bind(int sockfd, const struct sockaddr *src_addr, socklen_t src_addrlen,
          const struct sockaddr *dest_addr, socklen_t dest_addrlen) {
    if (sockfd < 0 || sockfd >= N) {
        errno = EBADF;
        return -1;
    }
    attach_shared_memory();

    P(sem_id);
    SM[sockfd].bind_request = 1;  // Set bind flag    
    // store source address
    memcpy(&SM[sockfd].src_addr, src_addr, src_addrlen);
    
    // store destination address
    memcpy(&SM[sockfd].dest_addr, dest_addr, dest_addrlen);    
    if (SM[sockfd].is_free) {
        V(sem_id);
        shmdt(SM);
        errno = EBADF;
        return -1;
    }

    // signal request and release shared memory
    V(sem_request);  
    V(sem_id);       

    // wait for bind to finish

    // block until initksocket handles request
    P(sem_req_rel); 
    // lock shared memory now
    P(sem_id);       

    if (SM[sockfd].udp_sockfd == -1) {
        V(sem_id);
        shmdt(SM);
        return -1;
    }


    printf("[k_bind] Socket %d bound to source and destination addresses.\n", sockfd);

    V(sem_id);
    shmdt(SM);
    return 0;
}

ssize_t k_sendto(int sockfd, const void *buf, size_t len, int flags,
                const struct sockaddr *dest_addr, socklen_t addrlen) {
    attach_shared_memory();
    if (sockfd < 0 || sockfd >= N) {
        errno = EBADF;
        // printf("err1\n");
        return -1;
    }

    P(sem_id);
    
    if (SM[sockfd].is_free) {
        V(sem_id);
        shmdt(SM);
        errno = EBADF;
        // printf("err2\n");
        return -1;
    }

    // check if destination matches with bound address
    struct sockaddr_in *dest = (struct sockaddr_in *)dest_addr;
    if (dest->sin_addr.s_addr != SM[sockfd].dest_addr.sin_addr.s_addr ||
        dest->sin_port != SM[sockfd].dest_addr.sin_port) {
        V(sem_id);
        shmdt(SM);
        errno = ENOTBOUND;
        // printf("err3\n");
        return -1;
    }

    // check if send buffer is full
    if (SM[sockfd].send_count >= SM[sockfd].swnd.size) {
    // if (SM[sockfd].send_count == 10) {
        V(sem_id);
        shmdt(SM);
        errno = ENOSPACE;
        // printf("err4\n");
        return -1;
    }

    // Add to send buffer
    int idx = SM[sockfd].send_tail;
    // size_t len = len > MSG_SIZE ? MSG_SIZE : len;
    memcpy(SM[sockfd].send_buf[idx], buf, len);
    SM[sockfd].send_len[idx] = len;
    SM[sockfd].swnd.seq_nums[SM[sockfd].swnd.next_seq] = SM[sockfd].send_tail;
    SM[sockfd].send_tail = (SM[sockfd].send_tail + 1) % 10;
    SM[sockfd].swnd.next_seq=(SM[sockfd].swnd.next_seq+1)%256;
    SM[sockfd].send_count++;

    printf("[k_sendto] Added message to send buffer (sockfd: %d, len: %zu)\n", sockfd, len);

    V(sem_id);
    shmdt(SM);
    return len;
}

ssize_t k_recvfrom(int sockfd, void *buf, size_t len, int flags,
                  struct sockaddr *src_addr, socklen_t *addrlen) {
    if (sockfd < 0 || sockfd >= N) {
        errno = EBADF;
        return -1;
    }
    attach_shared_memory();

    P(sem_id);
    
    if (SM[sockfd].is_free) {
        V(sem_id);
        shmdt(SM);
        errno = EBADF;
        return -1;
    }

    // check if receive buffer is empty
    if (SM[sockfd].recv_flag[SM[sockfd].recv_head] == 0) {
        V(sem_id);
        shmdt(SM);
        errno = ENOMESSAGE;
        return -1;
    }

    // get from receive buffer
    int idx = SM[sockfd].recv_head;
    SM[sockfd].recv_flag[SM[sockfd].recv_head] = 0;
    int seq = -1;
    for (int i = 0; i < 256; i++) if (SM[sockfd].rwnd.seq_nums[i] == SM[sockfd].recv_head) seq = i;
    SM[sockfd].rwnd.seq_nums[seq] = -1;
    SM[sockfd].rwnd.seq_nums[(seq+10)%256] = SM[sockfd].recv_head;
    SM[sockfd].rwnd.size++;
    size_t copy_len = SM[sockfd].recv_len[idx] > len ? len : SM[sockfd].recv_len[idx];
    memcpy(buf, SM[sockfd].recv_buf[idx], copy_len);
    
    SM[sockfd].recv_head = (SM[sockfd].recv_head + 1) % 10;

    int prev_nospace = SM[sockfd].nospace;
    SM[sockfd].nospace = (SM[sockfd].rwnd.size == 0);
    
    // if space was previously 0 but now available, set flag for thread R
    // to send a duplicate ACK with updated rwnd size
    if (prev_nospace && !SM[sockfd].nospace) {
        // this is already handled in R thread
    }

    // set src_addr if provided
    if (src_addr && addrlen) {
        memcpy(src_addr, &SM[sockfd].src_addr, *addrlen);
    }

    printf("[k_recvfrom] Retrieved message from receive buffer (sockfd: %d, len: %zu)\n", sockfd, copy_len);

    V(sem_id);
    shmdt(SM);
    return copy_len;
}

int k_close(int sockfd) {
    attach_shared_memory();
    if (sockfd < 0 || sockfd >= N) {
        errno = EBADF;
        return -1;
    }

    P(sem_id);
    if (SM[sockfd].is_free) {
        V(sem_id);
        shmdt(SM);
        errno = EBADF;
        return -1;
    }
    while(SM[sockfd].send_count){
        V(sem_id);
        sleep(5);
        P(sem_id);
    }
    tt=SM[sockfd].total_transmissions;
    SM[sockfd].is_free = 1;
    printf("[k_close] Socket %d closed.\n", sockfd);
    V(sem_id);
    shmdt(SM);
    return 0;
}
int dropMessage(float probability) {
    float r = (float)rand() / RAND_MAX;
    int drop = r < probability ? 1 : 0;
    // printf("[dropMessage] Drop message: %d (prob: %f, rand: %f)\n", drop, probability, r);
    return drop;
}