# KTP
Emulating End-to-End Reliable Flow Control over UDP

                                                Project Structure
The codebase is organized into the following files:

initksocket.c
    Contains initialization routines to set up shared memory and prepare the environment for socket creation and management.

ksocket.h
    Defines all key data structures, constants, and function prototypes used by the socket library.

ksocket.c
    Implements the core socket operations including creation, binding, sending, receiving, and closure. It also integrates 
    the sliding window and error handling mechanisms.

Data Structures

1. Window Data Structure for both sender and receiver (swindow and rwindow)
    
    Purpose:
        Implements the sliding window mechanism to manage packet transmission and acknowledgement efficiently.
    
    Key Components:
    size:
        Indicates the current window size, limiting the number of unacknowledged messages.
    base:
        Marks the starting sequence number for the active transmission window.
    next_seq:
        next expected sequence to send for sender.
        next expected inorder sequence to receiev to the buffer
    seq_nums[256]:
        An array used for managing both send and receive windows. In the send window, a value of -1 indicates an unsent or 
        acknowledged message, while a positive value references the message in the send buffer. 
        In the receive window, -1 marks an unexpected packet and positive values denote the expected order.       
    
2. Socket Management Entry (SM_entry)
    
    Purpose:
    Serves as the central structure for managing socket state and resources in shared memory.
    
    Detailed Components:
    
    Connection Identification:
    is_free: 
        Indicates whether the socket entry is available.    
    process_id: 
        Stores the process identifier linked to the socket.   
    udp_sockfd: 
        Maps the KTP socket to the underlying UDP socket.
    
    Address Configuration:
    struct sockaddr_in src_addr:  
        Represent the local endpoint’s IP and port.
    struct sockaddr_in dest_addr;  
        Represent the remote endpoint’s IP and port.
    
    Buffering Mechanisms:
    Send Buffer:
        A fixed array (send_buf[10][512]) to hold outgoing messages with associated sizes (send_len).
        Send count counts the number of msgs sent but not acknowledged.
        send_tail is used to add msg to that tail index.
        send_head is not particularly useful here as we can get it from base and seq_num

    Receive Buffer:
        A corresponding fixed array (recv_buf[10][512]) for incoming messages, along with flags markers (flags[10]), and message lengths (recv_len[10]).
        Recv_head is used to locate the next idx to get read from recv_buf.
        Recv_count and recv_tail are not particularly useful but we can use them to make code look simpler instead of base and flag.

    Window Management:
    swnd & rwnd:
    Control the send and receive sliding windows.

    timers[10] for storing time at sending.
    
    nospace: 
        Indicates if the receive buffer is full.

    socket_request & bind_request:
        Ensure that binding and UDP socket creation are executed only once.
    

    last_ack_seq:
        stores one minus the next seq with modulo.
    
    total_transmissions:
        stores the total number of transmissions done including retransmissions.

                                                            Core Functionalities
1. Socket Initialization and Creation
    Function: k_socket()
        Sets up a new KTP socket by allocating a free SM_entry, initializing the send and receive windows, and preparing the send/receive buffers.
    Key Operations: 
        Validates the socket type.
        Allocates and initializes the SM_entry.
        Sets the base values for both the swindow (base, next_seq) and the rwindow (expected_seq).

2. Data Transmission: k_sendto()
    Function: k_sendto()
        Transmits data packets using the swindow to ensure reliable delivery.
    Key Mechanisms:
        Assigns sequence numbers to packets using the swindow.
        Monitors acknowledgments to advance the send window.
        Uses timeout values (tracked in lastSendTime) to trigger retransmissions if needed.

3. Data Reception: k_recvfrom()
    Function: k_recvfrom()
        Receives incoming data, managing the rwindow to reorder out-of-sequence packets.
    Key Mechanisms:
        Buffers packets arriving out-of-order in the rwindow.
        Updates the expected_seq field once the missing packets are received.
        Delivers in-sequence packets to the application.

4. Binding and Socket Closure
    Functions: k_bind(), k_close()
    Binding Operations:
        Associates a socket with local and remote IP addresses and ports.
        Ensures that binding is performed only once.
    Closure Operations:
        Frees the SM_entry and resets buffers, swindow, and rwindow.

5. Packet Loss Simulation: dropMessage()
    Function: dropMessage()
        Simulates packet loss by probabilistically dropping packets to test retransmission mechanisms.

                                                        Thread and Shared Memory Management
Shared Memory:
    Provides a common area to store SM_entries, allowing multiple processes to access and manage socket resources efficiently.
    Implementation:
        SM_entries (including window structures and buffers) are stored in shared memory for dynamic and concurrent access.

Thread Structure
Receiver Thread:
    Continuously listens for incoming packets and processes them using the rwindow.
Sender Thread:
    Manages the transmission of packets, monitors for acknowledgments, and handles retransmissions by interfacing with the swindow.
Garbage Collection Thread:
    Periodically scans for inactive sockets and reclaims associated resources to prevent memory leaks.



                                                    Table
file size is 102400 bytes.

probability   original          Total           Avg
    P        transmissions   transmissions  transmissions

    0.05        201             239            1.19
    0.10        201             285            1.42
    0.15        201             317            1.58
    0.20        201             355            1.77
    0.25        201             414            2.06
    0.30        201             492            2.45
    0.35        201             518            2.58
    0.40        201             535            2.66
    0.45        201             548            2.73
    0.50        201             675            3.36


you may use this as an example to test:

make clean
make
./initksocket
./user2 127.0.0.1 5001 127.0.0.1 5002 output.txt
./user1 127.0.0.1 5002 127.0.0.1 5001 input.txt
