/*
Name: Basavaraj pagad
Project: TFTP (Trivial File Transfer Protocol)
Description:--
This project implements a simplified TFTP (Trivial File Transfer Protocol) client-server
application using the C programming language over UDP sockets. It replicates the core
behavior of RFC 1350 TFTP, including read requests (RRQ) and write requests (WRQ),
block-numbered DATA/ACK transfer with retransmission on timeout, and both octet and
netascii transfer modes. The client provides an interactive command-line interface
supporting connect, PUT (upload), GET (download), mode selection, and exit. The server
listens for incoming requests, forks a new process per client, and communicates over a
dedicated ephemeral UDP socket for each transfer, allowing multiple simultaneous file
transfers. The system demonstrates practical applications of socket programming,
UDP-based reliable transfer design, process forking, and file I/O in a networked
client-server environment.
*/

#include "tftp.h"
#include "errno.h"
#include "tftp_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

int connect_flag = 0;

// client main 
int main() 
{
    char command[100];
    tftp_client_t client;

    memset(&client, 0, sizeof(client));  // Initialize client structure

    strcpy(client.mode_choise, "default");
    client.data_size = 512;
    // Main loop for command-line interface
    while (1) 
    {
        printf("tftp> ");
        printf("\n1.connect...\n2.PUT...\n3.GET...\n4.MODE...\n5.Disconnect...\n6.EXIT...\n");
        fgets(command, sizeof(command), stdin);

        // Remove newline character
        command[strcspn(command, "\n")] = 0;

        if(strcmp(command, "6") == 0)
        {
            printf("App is closing....\n");
            disconnect(&client);
            break;
        }
        // Process the command
        process_command(&client, command);
    }

    return 0;
}

// Function to process commands
void process_command(tftp_client_t *client, char *command) 
{
    switch(*command)// Check which menu command was selected
    {
        case '1':
        {
            if(connect_flag == 0)// Check whether the client is already connected
            {
                char ip[INET_ADDRSTRLEN];
                printf("Enter the ip address of server : ");
                if(fgets(ip, sizeof(ip), stdin) == NULL)// Read IP address from user
                {
                    printf("Failed to read IP address...\n");
                    return;
                }
                ip[strcspn(ip, "\n")] = 0; // Remove newline character from IP address
                connect_flag = 1;
                connect_to_server(client, ip, PORT);// Connect client socket to the server
            }
            else
            {
                printf("This connection is alredy do...\n");
            }
            
            break;
        }

        case '2':
        {
            if(connect_flag == 1)// Check whether client is connected
            {
                char filename[100];
                printf("Enter the filename : ");
                if(fgets(filename, sizeof(filename), stdin) == NULL)// Read filename from user
                {
                    printf("Failed to read file name...\n");
                    return;
    
                }
                filename[strcspn(filename, "\n")] = 0;// Remove newline character from filename
                
                if(is_file_valid(filename) == -1)// Validate the filename
                {
                    printf("File is invalid...\n");
                    return;
                }
                put_file(client, filename);// Upload file to the server
            }
            else
            {
                printf("Errror: First connect the device...\n");
            }
            break;
        }

        case '3':
        {
            if(connect_flag == 1) // Check whether client is connected
            {
                char filename[100];
                printf("Enter the filename : ");
                if(fgets(filename, sizeof(filename), stdin) == NULL)// Read filename from user
                {
                    printf("Failed to read file name...\n");
                    return;
    
                }
                filename[strcspn(filename, "\n")] = 0;// Remove newline character from filename
                
                if(is_file_valid(filename) == -1) // Validate the filename
                {
                    printf("File is invalid...\n");
                    return;
                }

                get_file(client, filename); // Download file from the server
            }
            else
            {
                printf("Errror: First connect the device...\n");
            }
 
           break;
        }
        
        case '4':
        {
            mode_file(client);// Change the file transfer mode
            break;
        }

        case '5':
            if(connect_flag == 1)
            {
                disconnect(client);// Disconnect from the server
                printf("Device is disconnected...\n");
            }
            else
            {
                printf("NO device is connected to client....\n");
            }
            break;

        default:
            printf("The give input is in valid...\n");
            
    } 
   
}

// File size changeing 
void mode_file(tftp_client_t *client)
{
    int choice;

    // Display available transfer modes
    printf("\n1.Default\n");
    printf("2.Octet\n");
    printf("3.Netascii\n");

    printf("Enter choice : ");

    scanf("%d",&choice);

    getchar();// Remove newline left by scanf()


    switch(choice)// Process selected mode
    {
        case 1:
            // Set transfer mode to default
            strcpy(client->mode_choise,"default");

            client->data_size = 512;// Set default data block size

            printf("Default mode selected\n");

            break;


        case 2:

            strcpy(client->mode_choise,"octet");// Set transfer mode to octet

            client->data_size = 64;// Set octet data block size

            printf("Octet mode selected\n");

            break;


        case 3:

            strcpy(client->mode_choise,"netascii");// Set transfer mode to netascii

            client->data_size = 512;// Set netascii data block size

            printf("Netascii mode selected\n");

            break;


        default:

            printf("Invalid choice\n");

    }
}

// File validation
int is_file_valid(char *filename)
{
    int len = strlen(filename);// Calculate filename length
    if(len == 0)
    {
        return -1;
    }

    const char *illegal = "/\\:*?\"<>|";// List of characters that are not allowed in filename

    for(int i = 0; i < len; i++)// Check every character in filename
    {
        if(strchr(illegal, filename[i]) != NULL)// Check whether current character is illegal
        {
            return -1;
        }
    }

    char *dot = strchr(filename, '.');// Find first dot in filename

    // Reject filename without extension or invalid dot position
    if(dot == NULL || filename[0] == '.' || filename[len-1] == '.')
    {
        return -1;
    }

    return 0;
}

// Address validation 
int add_vali(char *ip)
{
    // Check whether IP address is too long
    if (strlen(ip) >= INET_ADDRSTRLEN)
    {
        return 1;
    }

    // Check for consecutive dots or dot at beginning/end
    if (strstr(ip, "..") != NULL || ip[0] == '.' || ip[strlen(ip) - 1] == '.')
    {
        return 1;
    }

    char temp_ip[INET_ADDRSTRLEN];
    strncpy(temp_ip, ip, sizeof(temp_ip) - 1);// Copy IP address into temporary buffer
    temp_ip[sizeof(temp_ip) - 1] = '\0';// Ensure temporary string is NULL terminated

    int segment_count = 0;
    char *token = strtok(temp_ip, "."); // Split IP address using dot separator

    while (token != NULL)// Process every IP segment
    {
        segment_count++;
        int i = 0;
        while (token[i] != '\0')// Check every character in current segment
        {
            if (!isdigit((unsigned char) token[i]))// Check whether character is a digit
            {
                return 1;
            }
            i++;
        }

        int value = atoi(token);// Convert current segment from string to integer
        if (value < 0 || value > 255)
        {
            return 1;
        }

        token = strtok(NULL, ".");// Get next IP segment
    }

    if (segment_count != 4)
    {
        return 1;
    }

    return 0;
}

// connecting the server ip address 
void connect_to_server(tftp_client_t *client, char *ip, int port) 
{
    //ip_address validating
    int ret = add_vali(ip);
    if(ret == 1)
    {
        connect_flag = 0;
        printf("IP address is invalidate...\n");
        return;
    }

    //copying the valid ip_address...
    strcpy(client->server_ip, ip);
    
    // Create UDP socket
    client-> sockfd = socket(AF_INET, SOCK_DGRAM,0);

    if (client->sockfd == -1)
    {
        connect_flag = 0;
        printf("socket() failed: %s\n", strerror(errno));
        return;
    }

    // Set socket timeout option
    struct timeval tv = { .tv_sec = TIMEOUT_SEC, .tv_usec = 0 };
    setsockopt(client->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Set up server address
    memset(&client-> server_addr, 0, sizeof(client-> server_addr));
    client-> server_addr.sin_family = AF_INET;
    client-> server_addr.sin_port = htons(port);
    client-> server_addr.sin_addr.s_addr = inet_addr(client->server_ip);

    client-> server_len = sizeof(client-> server_addr);// Store size of server address structure
    
    connect_flag = 1;
    printf("Connected (socket ready) to %s:%d\n", client->server_ip, port);
}

// put function 
void put_file(tftp_client_t *client, char *filename)
{
    // Reset destination to well-known TFTP server port
    client->server_addr.sin_port = htons(PORT);

    int fd = open(filename, O_RDONLY);

    if(fd == -1)
    {  
        perror("open");            
        return;
    }

    tftp_packet D_packet;
    memset(&D_packet, 0, sizeof(D_packet));

    strcpy(D_packet.body.request.filename, filename);

    // Send WRQ request and send file..
    struct sockaddr_in request_addr = client->server_addr;
    request_addr.sin_port = htons(PORT);
    send_request(client->sockfd, request_addr, filename, client->mode_choise, WRQ);


    // Send RRQ and recive file 
    struct sockaddr_in transfer_addr; 
    socklen_t transfer_len = sizeof(transfer_addr); 
    // Wait for ACK block 0 from server
    ssize_t recv_size = recvfrom( client->sockfd, &D_packet, sizeof(D_packet), 0, (struct sockaddr *)&transfer_addr, &transfer_len );
    if(recv_size == -1)
    {
        perror("recvfrom");
        close(fd);
        return;
    }

    //checking the returned ack bit...
    if(ntohs(D_packet.opcode) == ACK && ntohs(D_packet.body.ack_packet.block_number) == 0)
    {
        printf( "Transfer started on server port %d\n", ntohs(transfer_addr.sin_port) );
        // Send file data to server
        send_file( client->sockfd, transfer_addr, transfer_len, fd, client->data_size, client->mode_choise );
    }
    else if (ntohs(D_packet.opcode) == ERROR)
    {
        printf("Server error: %s\n", D_packet.body.error_packet.error_msg);
        close(fd);
    }
    else
    {
        printf("Unexpected response from server, PUT failed...\n");
        close(fd);
    }
}

// get function 
void get_file(tftp_client_t *client, char *filename) 
{
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1)
    {
        perror("open");
        return;
    }

     struct sockaddr_in request_addr = client->server_addr;
     request_addr.sin_port = htons(PORT);// Use well-known TFTP port for RRQ

    // Send RRQ and recive file 
    send_request(client->sockfd, request_addr, filename, client->mode_choise, RRQ);


    struct sockaddr_in transfer_addr; 
    socklen_t transfer_len = sizeof(transfer_addr);

    tftp_packet packet;

    // Receive first DATA packet from server
   ssize_t recv_size = recvfrom( client->sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&transfer_addr, &transfer_len );
   if(recv_size == -1) 
   { 
        perror("recvfrom DATA"); 
        close(fd); 
        return; 
    }

    // Check whether server returned an ERROR packet
    if(ntohs(packet.opcode) == ERROR)
    {
        printf("Server error: %s\n", packet.body.error_packet.error_msg);
        close(fd);
        return;
    }

    // Check whether received packet is DATA
    if(ntohs(packet.opcode) != DATA)
    {
        printf("Unexpected packet received\n");
        close(fd);
        return;
    }

    // Check whether first DATA block number is 1
    if(ntohs(packet.body.data_packet.block_number) != 1)
    {
        printf("Unexpected DATA block\n");
        close(fd);
        return;
    }

    printf("Transfer started on server port %d\n", ntohs(transfer_addr.sin_port));

    /*
     * WRITE DATA BLOCK 1
     */
    // Calculate actual data size excluding opcode and block number
    ssize_t data_size = recv_size - sizeof(packet.opcode) - sizeof(packet.body.data_packet.block_number);

    if(write(fd, packet.body.data_packet.data, data_size) < 0)
    {
        perror("write");
        close(fd);
        return;
    }

    /*
     * SEND ACK BLOCK 1
     */
    tftp_packet ack;

    memset(&ack, 0, sizeof(ack));

    ack.opcode = htons(ACK);

    ack.body.ack_packet.block_number =htons(1);

    // Send ACK for block 1
    sendto(client->sockfd, &ack, sizeof(ack.opcode) + sizeof(ack.body.ack_packet.block_number),
    0, (struct sockaddr *)&transfer_addr, transfer_len);

    /*
     * If DATA block is less than 512 bytes,
     * transfer is complete.
     */
    if(data_size < 512)
    {
        printf(
            "Transfer complete: received 1 block(s)\n"
        );

        close(fd);
        return;
    }

    /*
     * More blocks exist.
     *
     * You need receive_file() to continue
     * from block 2.
     */

    close(fd);

}

// Disconnecting 
void disconnect(tftp_client_t *client) 
{
    // close fd
    if(client->sockfd > 0)// Check whether socket descriptor is valid
    {
        close(client->sockfd);
        client->sockfd = 0;
    }
    connect_flag = 0;
}

// send request 
void send_request(int sockfd, struct sockaddr_in server_addr, char *filename, char *mode,int opcode)
{
    tftp_packet client;
    memset(&client, 0, sizeof(client));
    client.opcode = htons(opcode);

    // Copy filename into request packet
    strncpy(client.body.request.filename, filename, sizeof(client.body.request.filename)-1);
    // Copy transfer mode into request packet
    strncpy(client.body.request.mode, mode, sizeof(client.body.request.mode)-1);

    // Calculate actual request packet length
    size_t packet_len = sizeof(client.opcode) + strlen(client.body.request.filename) + 1 + strlen(client.body.request.mode) + 1;

    // Send RRQ or WRQ packet to server
    ssize_t s_size = sendto(sockfd, &client, packet_len, 0,(struct sockaddr *)&server_addr, sizeof(server_addr));
    if(s_size == -1)
    {
        perror("sendto");
        return;
    }
}
