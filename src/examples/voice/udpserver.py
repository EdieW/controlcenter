#!/usr/bin/env python3

import socket
import sys

# Create a UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Bind the socket to the port
server_address = ('0.0.0.0', 17002)
print('starting up on {} port {}'.format(*server_address))
sock.bind(server_address)

text_file = open("Output.txt", "a")

while True:
    print('\nwaiting to receive message')
    data, address = sock.recvfrom(1024)

    print('received {} bytes from {}'.format(
        len(data), address))
    print(data)

    if data:
        str = data.decode()
        text_file.write(str + "\n")
        temperature,humidity = str.split(',')
        t = float(temperature)
        h = int(humidity)
        sent = sock.sendto("OK".encode(), address)
        print('sent {} bytes back to {}'.format(
            sent, address))
        if (t < 25.0):
           print('low temp {} humid {}'.format(t, h))

text_file.close()
