bigfile_http
============

A Simple HTTP Network Benchmark Server

Overview
========
This server does nothing but serves 1GB files through `HTTP 1.1` protocol. It generates file on the fly thus no disk space would be wasted. 

Compiling
=========
**Note: Since this program is using `epoll`, it will only compile under Linux. **

	$ gcc -o bigfile -Wall -std=gnu11 bigfile.c
	
Usage
=====
The usage of this software is trivial. Just run it as:

	$ ./bigfile port
	
Where port should be a valid port number that bigfile_http should be listening on. 

Once the server starts, you can run the benchmark like this:

	$ wget -O /dev/null http://[your_ip]:your_port

Author
======
Datong Sun (dndx [at] idndx [dot] com)

Licence
=======
This code is released under The MIT License (MIT). See `LICENSE` file for more information. 