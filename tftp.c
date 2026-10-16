/* Common file for server & client */

#include "tftp.h"
#include "tftp_client.h"

//converting to windows file
void convert_to_netascii(char *data, ssize_t *size)
{
    char temp[512];
    int j = 0; 

    for (int i = 0; i < *size; i++)// Process every byte of input data
    {
        if (data[i] == '\n')
        {
            if (j + 2 > (int)sizeof(temp))// Check temporary buffer space
            {
                break;
            }
            temp[j++] = '\r';// Add carriage return before newline
            temp[j++] = '\n';// Add newline character
        }
        else
        {
            if (j + 2 > (int)sizeof(temp)) // Check temporary buffer space
            {
                break;
            }
            temp[j++] = data[i];// Copy normal character
        }
    }

    memcpy(data, temp, j);//Copy converted data back to original buffer

    *size = j;// Update size after conversion
}

//converting to linux file 
void convert_from_netascii(char *data, ssize_t *size)
{
    char temp[512];
    int j = 0;

    for (int i = 0; i < *size; i++)// Process every received byte
    {
        if (data[i] == '\r' && data[i + 1] == '\n' && i + 1 < *size)
        {
            temp[j++] = '\n';// Convert CR-LF into Linux newline
            i++; // skip '\n'
        }
        else
        {
            temp[j++] = data[i];// Copy normal character
        }
    }

    memcpy(data, temp, j);// Copy converted data back to original buffer

    *size = j;// Update size after conversion
}

/*
 * Basic filename safety check for anything coming coming off the wire from a
 * remote RRQ/WRQ. Rejects absolute paths and parent-directory traversal.
 */
int sanitize_filename(const char *filename)
{
    if (filename == NULL || filename[0] == '\0')//Check for NULL or empty filename
    {
        return -1;
    }

    if (filename[0] == '/')// Check for absolute path
    {
        return -1; // no absolute paths
    }

    if (strstr(filename, "..") != NULL)// Check for parent-directory traversal
    {
        return -1; // no parent directory traversal
    }

    return 0;
}

// real send function 
void send_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, int fd, int size, char *mode)
{
    // Implement file sending logic here
    tftp_packet D_packet;
    uint16_t block_number = 0;// Initialize TFTP block number

    ssize_t bytes_read;   // Store number of bytes read from file
    int last_block_was_full;// Indicates whether the last block was full

    do
    {
        //assigning the 0 to D_packet struct for safty...
        memset(&D_packet, 0, sizeof(D_packet));
        bytes_read = read(fd, D_packet.body.data_packet.data, size);// Read file data
        if(bytes_read < 0)// Check whether read failed
        {
            perror("read");
            close(fd);
            return;
        }

        block_number++;// Increment TFTP block number

        // cheacking the mode....
        if(strcmp(mode,"netascii") == 0)
        {
            convert_to_netascii(D_packet.body.data_packet.data, &bytes_read);// Convert LF to CR-LF
        }

        D_packet.opcode = htons(DATA);// Set DATA opcode in network byte order

        D_packet.body.data_packet.block_number = htons(block_number);

        // Calculate actual packet length
        size_t packet_len = sizeof(D_packet.opcode) + sizeof(D_packet.body.data_packet.block_number) + bytes_read;

        int retries = 0;
        int acked = 0;

        while(retries < 10 && !acked)// Retry until ACK or maximum retries
        {
            // Send DATA packet
            if(sendto(sockfd, &D_packet, packet_len, 0, (struct sockaddr *)&client_addr, client_len) == -1)
            {
                perror("sendto");
                retries++;
                continue;
            }

            tftp_packet rec_data;// Structure to store received ACK

            // Wait for ACK
            ssize_t ack_size = recvfrom(sockfd, &rec_data, sizeof(rec_data), 0, (struct sockaddr *) &client_addr, &client_len);
            if(ack_size == -1)// Check if receiving ACK failed
            {
                perror("recvfrom (ack)");
                retries++;
                continue;
            }

            // Check correct ACK
            if(ntohs(rec_data.opcode) == ACK && ntohs(rec_data.body.ack_packet.block_number) == block_number)
            {
                acked = 1;// Mark block as successfully acknowledged
            }
            else
            {
                retries++;// Retry if ACK is incorrect
            }
        }
        if(!acked)// Check if all retries failed
        {
            fprintf(stderr, "send_file: no ACK for block %u after 10 retries, aborting\n", block_number);
            close(fd);
            return;
        }


        last_block_was_full = ((size_t)bytes_read == (size_t)size);// Check if block is full

        // If block is smaller than buffer size
        if (!last_block_was_full)
        {
            printf("Transfer complete: sent %u block(s) successfully.\n", block_number);
        }

    }while(last_block_was_full);

    close(fd);
}


// real receive function
void receive_file(int sockfd, struct sockaddr_in client_addr, socklen_t client_len, int fd, int size, char *mode)
{
    // Implement file receiving logic here
    tftp_packet rec_data;
    ssize_t recv_size;
    uint16_t block_number = 1;

    while(1)// Continue receiving until transfer completes
    {
        memset(&rec_data, 0, sizeof(rec_data));
         // Receive DATA packet
        recv_size = recvfrom(sockfd, &rec_data, sizeof(rec_data), 0, (struct sockaddr *)&client_addr, &client_len);

        if(recv_size == -1)
        {
            perror("recvfrom");
            close(fd);
            return;
        }

        if(ntohs(rec_data.opcode) == ERROR)// Check if server sent an ERROR packet
        {
            fprintf(stderr, "Server error: %s\n", rec_data.body.error_packet.error_msg);
            close(fd);
            return;
        }

        if(ntohs(rec_data.opcode) != DATA)// Ignore packets that are not DATA
        {
            continue;
        }

        uint16_t recv_block = ntohs(rec_data.body.data_packet.block_number);// Extract received block number

        // this for last  block info....
        if( recv_block == (uint16_t)(block_number-1))// Check for duplicate previous block
        {
            tftp_packet ack;
            memset(&ack, 0, sizeof(ack));
            ack.opcode = htons(ACK);
            ack.body.ack_packet.block_number = htons(recv_block);// Set previous block number
            // Re-send ACK for duplicate packet
            sendto(sockfd, &ack, sizeof(ack.opcode) + sizeof(ack.body.ack_packet.block_number), 0, (struct sockaddr *)&client_addr, client_len);
            continue;
        }

         // Check whether received block is unexpected
        if(recv_block != block_number)
        {
            continue;
        }

        ssize_t data_size;
        // Calculate data size
        data_size = recv_size - sizeof(rec_data.opcode) - sizeof(rec_data.body.data_packet.block_number);

        if(strcmp(mode, "netascii") == 0)// Check for netascii mode
        {
            convert_from_netascii(rec_data.body.data_packet.data, &data_size);// Convert CR-LF to LF
        }

        if(data_size > 0 &&  write(fd, rec_data.body.data_packet.data, data_size) < 0)// Write data to file
        {
            perror("write");
            close(fd);
            return;
        }

        tftp_packet ack;
        memset(&ack, 0, sizeof(ack));
        ack.opcode = htons(ACK);
        ack.body.ack_packet.block_number = htons(block_number);// Set received block number

        // Send ACK to sender
        sendto(sockfd, &ack, sizeof(ack.opcode)+sizeof(ack.body.ack_packet.block_number), 0, (struct sockaddr *)&client_addr, client_len);
        if(data_size < size)
        {
            printf("Transfer complete: received %u block(s), file written successfully.\n", block_number);
            break;
        }


        block_number++;// Move to the next expected block
    }

    close(fd);// Close file after receiving
}

