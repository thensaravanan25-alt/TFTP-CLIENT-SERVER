#include "tftp.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

static void handle_client(struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet);
static int make_data_socket(void);

/*<----------- server main -------->*/
int main() 
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    tftp_packet packet;

    //Reap finished per-transfer children automatically, no zombies
    signal(SIGCHLD, SIG_IGN);

    // Create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd == -1)
    {
        perror("socket");
        exit(1);
    }
    
    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if(bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("bind");
        close(sockfd);
        exit(1);
    }

  

    printf("TFTP Server listening on port %d...\n", PORT);

    // Main loop to handle incoming requests
    while (1) {
        memset(&packet, 0, sizeof(packet));
        client_len = sizeof(client_addr);

        int n = recvfrom(sockfd, &packet, BUFFER_SIZE, 0, (struct sockaddr *)&client_addr, &client_len);
        if (n < 0) {
            perror("Receive failed or timeout occurred");
            continue;
        }

        /*
        * Creating the process for executing the handle_client function
        * if any error comes means the child terminating by using exit system call main will not effected.
        */
        pid_t pid = fork();
        if(pid == 0)
        {
            close(sockfd);
            handle_client(client_addr, client_len, &packet);
            exit(0);
        }
        else if(pid < 0)
        {
            perror("fork");
        }
    }

    close(sockfd);
    return 0;
}

// makeing default socket 
static int make_data_socket(void)
{
    // Create a UDP socket for the individual TFTP transfer
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s == -1)
    {
        perror("socket(per - transfer)");
        return -1;
    }

    // Set socket timeout option
    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    //TODO Use setsockopt() to set timeout option
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct  sockaddr_in any_addr;
    memset(&any_addr, 0, sizeof(any_addr));
    any_addr.sin_family = AF_INET;
    any_addr.sin_addr.s_addr = INADDR_ANY;//default 0.0.0.0
    any_addr.sin_port = 0; // OS picks an ephemeral port
 
    // Bind the socket to an automatically selected port
    if (bind(s, (struct sockaddr *)&any_addr, sizeof(any_addr)) == -1)
    {
        perror("bind (per-transfer)");
        close(s);
        return -1;
    }
 
    return s;
}

// error sending function 
static void send_error(int sockfd, struct sockaddr_in *addr, socklen_t addr_len, uint16_t code, const char *msg)
{
    tftp_packet err;
    memset(&err, 0, sizeof(err));
    err.opcode = htons(ERROR);
    err.body.error_packet.error_code = htons(code); // Set TFTP error code
    // Copy error message into packet
    strncpy(err.body.error_packet.error_msg, msg, sizeof(err.body.error_packet.error_msg)-1);

    // Calculate actual size of the error packet
    size_t len = sizeof(err.opcode) + sizeof(err.body.error_packet.error_code) + strlen(err.body.error_packet.error_msg) + 1;
    sendto(sockfd, &err, len, 0, (struct sockaddr *)addr, addr_len);// Send error packet to the client
}

// client handler function 
static void handle_client(struct sockaddr_in client_addr, socklen_t client_len, tftp_packet *packet) 
{
    // Extract the TFTP operation (read or write) from the received packet
    // and call send_file or receive_file accordingly

    // converting network data to host data type...
    uint16_t opcode = ntohs(packet->opcode);

    if(opcode != RRQ && opcode != WRQ)
    {
        fprintf(stderr, "Unexpected opcode %u from client, ignoring\n", opcode);
        return;
    }

    // creating the file and mode buffer size and initalizing will null for safer.....
    packet->body.request.filename[sizeof(packet->body.request.filename) - 1] = '\0';
    packet->body.request.mode[sizeof(packet->body.request.mode) - 1] = '\0';
    
    // Create a separate UDP socket for this transfer
    int data_sock = make_data_socket();
    if(data_sock == -1)
    {
        return;
    }

    // Validate the requested filename
    if (sanitize_filename(packet->body.request.filename) == -1)
    {
        fprintf(stderr, "Rejected unsafe filename: %s\n", packet->body.request.filename);
        // Send invalid filename error to client
        send_error(data_sock, &client_addr, client_len, 2, "Invalid filename");
        close(data_sock);
        return;
    }

    const int block_size = 512;// TFTP DATA packet size is normally 512 bytes

    if(opcode == RRQ)// Handle Read Request
    {
        // Open requested file in read-only mode
        int fd = open(packet->body.request.filename, O_RDONLY);
        if(fd == -1)// Check whether file opening failed
        {
            perror("open (RRQ)");
            // Inform client that file was not found
            send_error(data_sock, &client_addr, client_len, 1, "File not found");
            close(data_sock);
            return;
        }
        // Send the requested file to the client
        send_file(data_sock, client_addr, client_len, fd, block_size, packet->body.request.mode);
    }
    else
    {
         // Open or create requested file for writing
        int fd = open(packet->body.request.filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1)
        {
            perror("open (WRQ)");
            // Inform client that file cannot be created
            send_error(data_sock, &client_addr, client_len, 2, "Cannot create file");
            close(data_sock);
            return;
        }
 
        tftp_packet ack0;
        memset(&ack0, 0, sizeof(ack0));
        ack0.opcode = htons(ACK);
        ack0.body.ack_packet.block_number = htons(0);
        // Send ACK 0 to tell client that server is ready to receive data
        sendto(data_sock, &ack0, sizeof(ack0.opcode) + sizeof(ack0.body.ack_packet.block_number), 0, (struct sockaddr *)&client_addr, client_len);
 
        // Receive the file from the client
        receive_file(data_sock, client_addr, client_len, fd, block_size, packet->body.request.mode);
    }
 
    close(data_sock);
    
}