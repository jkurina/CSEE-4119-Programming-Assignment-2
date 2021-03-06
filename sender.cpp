#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/time.h>
#include "tcp.h"
using namespace std;

int main(int argc, char *argv[])
{
	if (argc < 6) {
		cout << "Usage: ./sender <filename> <remote_IP> <remote_port> <ack_port_num> <log_filename>" << endl;
		exit(1);
	}
	ifstream file(argv[1]);
	if (!file.is_open()) {
		cout << "Could not open " << argv[1] << endl;
		exit(1);
	}
	ofstream logFile;
	streambuf *logBuffer;
	if (string(argv[5]) != "stdout") {
		logFile.open(argv[5]);
		if (!logFile.is_open()) {
			cout << "Could not create " << argv[5] << endl;
			exit(1);
		}
		logBuffer = logFile.rdbuf();
	} else {
		logBuffer = cout.rdbuf();
	}
	ostream log(logBuffer);
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	int ackfd = socket(AF_INET, SOCK_DGRAM, 0);
	struct timeval timeout;
	timeout.tv_sec = 2;
	timeout.tv_usec = 0;
	if (setsockopt(ackfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1) {
		perror("error");
		exit(1);
	}
	struct sockaddr_in receiver;
	struct sockaddr_in sender;
	receiver.sin_family = AF_INET;
	receiver.sin_port = htons(atoi(argv[3]));
	if (!inet_aton(argv[2], &receiver.sin_addr)) {
		cout << argv[2] << " is not a valid IP address" << endl;
		exit(1);
	}
	sender.sin_family = AF_INET;
	sender.sin_port = htons(atoi(argv[4]));
	sender.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(ackfd, (struct sockaddr *) &sender, sizeof(sender)) == -1) {
		perror("error");
		exit(1);
	}
	int sequenceNumber = 0;
	int acknowledgment_number = 0;
	struct tcp_packet packet;
	int n;
	int t;
	int segments = 0;
	int segmentsRetransmitted = 0;
	struct sockaddr_in ephemeral;
	socklen_t ephemeralSize;
	while (!file.eof()) {
		bzero(&packet, sizeof(packet));
		packet.header.source_port = sender.sin_port;
		packet.header.destination_port = receiver.sin_port;
		packet.header.sequence_number = sequenceNumber;
		packet.header.acknowledgment_number = acknowledgment_number;
		packet.header.offset_and_flags = 5 << 4;
		file.read(packet.payload, MSS - 20);
		n = file.gcount();
		if (n < MSS - 20) {
			packet.header.offset_and_flags |= 1;
		}
		/* Using C implementation of Internet checksum from https://tools.ietf.org/html/rfc1071 */
		int packetSize = n + HEADER_SIZE;
		unsigned short *ptr = (unsigned short *) &packet;
		int sum = 0;
		while (packetSize > 1) {
			sum += *ptr++;
			packetSize -= 2;
		}
		if (packetSize > 0)
			sum += * (unsigned char *) ptr;
		while (sum >> 16)
			sum = (sum & 0xFFFF) + (sum >> 16);
		packet.header.checksum = ~sum;
		packetSize = n + HEADER_SIZE;
		int newSequenceNumber = sequenceNumber + packetSize;
		int retransmitted = -1;
		struct timeval start, end;
		do {
			log << (int) time(NULL) << ",\t" << inet_ntoa(sender.sin_addr) << ",\t" << argv[2] << ",\t" << packet.header.sequence_number << ",\t" << packet.header.acknowledgment_number << ",\tACK: " << ((packet.header.offset_and_flags & (1 << 4)) >> 4) << "\tFIN: " << (packet.header.offset_and_flags & 1) << ",\t" << timeout.tv_sec + timeout.tv_usec / 1000000.0 << endl;
			gettimeofday(&start, NULL);
			if (sendto(sockfd, &packet, packetSize, 0, (struct sockaddr *) &receiver, sizeof(receiver)) == -1) {
				perror("error");
				exit(1);
			}
			retransmitted++;
			t = recvfrom(ackfd, &packet, sizeof(packet.header), 0, (struct sockaddr *) &ephemeral, &ephemeralSize);
		} while (t < HEADER_SIZE || !(packet.header.offset_and_flags & (1 << 4)) || packet.header.acknowledgment_number != newSequenceNumber);
		gettimeofday(&end, NULL);
		log << (int) time(NULL) << ",\t" << inet_ntoa(ephemeral.sin_addr) << ",\t" << inet_ntoa(sender.sin_addr) << ",\t" << packet.header.sequence_number << ",\t" << packet.header.acknowledgment_number << ",\tACK: " << ((packet.header.offset_and_flags & (1 << 4)) >> 4) << "\tFIN: " << (packet.header.offset_and_flags & 1) << ",\t" << timeout.tv_sec + timeout.tv_usec / 1000000.0 << endl;
		double difference = (end.tv_sec * 1000000 + end.tv_usec) - (start.tv_sec * 1000000 + start.tv_usec);
		double estimatedRTT = (timeout.tv_sec * 1000000 + timeout.tv_usec);
		estimatedRTT = (1 - 0.125) * estimatedRTT + 0.125 * difference;
		timeout.tv_sec = estimatedRTT / 1000000;
		timeout.tv_usec = estimatedRTT - timeout.tv_sec * 1000000;
		acknowledgment_number = packet.header.sequence_number + HEADER_SIZE;
		sequenceNumber = newSequenceNumber;
		segments++;
		if (retransmitted > 0)
			segmentsRetransmitted++;
	}
	file.close();
	if (logFile.is_open())
		logFile.close();
	close(sockfd);
	close(ackfd);
	cout << "Delivery completed successfully" << endl;
	cout << "Total bytes sent = " << sequenceNumber << endl;
	cout << "Segments sent = " << segments << endl;
	cout << "Segments retransmitted = " << segmentsRetransmitted << endl;
	return 0;
}
