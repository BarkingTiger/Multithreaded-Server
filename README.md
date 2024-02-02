## To build program: 
<p>
make [all, httpserver]
</p>

## SYNOPSIS
<p>
 This file contains the implementation of a multithreaded HTTP Server.
</p>

## Design
<p>
The HTTP Server is an addon to the HTTP Server done in asgn1. Instead of being single threaded, it uses a thread pool to process the requests. For the threads to recieve requests, a bounded buffer is used to store the file desciptors of the clients, which is then used by the threads. To make sure no collisions happen, flock() is used to make sure the files are atomically read/written to. If I had more time, I may have used fctnl(). As for the log, I wrote to file after every request, this is slower than to just buffering the writes.
</p>

