# TCP-based File Transfer Server and CLient Application

This is a demonstration project I made to show how to transfer files over TCP Sockets, written in C.

**CLIENT:**
The client application connects to a server, which then requests a filename or file path. If the server can access a file witht the same name it sends it back to the client as a data stream.
```
Usage:
./client 127.0.0.1 9001
```

**SERVER:**
The server implements a thread pool and can therefore respond to multiple clients simultaneously. Files are deconstructed into a data stream and sent to clients.
```
Usage:
./server 9001
```
