# A debugging web proxy
Yes, not for compression or caching, but for **debugging**.

## It can work transparently.
you can set an absolute address.  That is to say that all traffic going to port, say 85, should be forwarded 
to port 80, after it's dumped to the screen of course.

Here we see the HTTP Headers, quite clearly.

	 1 GET /favicon.ico HTTP/1.1
	 1 Accept: */*
	 1 Accept-Encoding: gzip, deflate
	 1 User-Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; WOW64; Trident/5.
	 - 0)
	 1 Host: 10.10.131.136:7790
	 1 Connection: Keep-Alive
	 1 Cookie: sess=9232fab8797f86fd473274cb66e3a923
	 1 
	 1 }

That was the end of that chunk of data, called a packet, duh.
After each chunk you can see how many connections are open and
whether they are reading from the client, writing to the client
or reading from the server or writing the server.

Connection 1 had read from the client and wrote to the server

	 0:rR    1:r  W 
	 0:rR    1:rR   

Now connection 1 will read from the server and write to the client

	 1 HTTP/1.1 404 Not Found
	 1 Date: Thu, 21 Jul 2011 23:44:15 GMT
	 1 Status: 404 Not Found
	 1 Connection: keep-alive
	 1 Content-Type: text/html
	 1 
	 0:rR    1:r w  
	 0:rR    1:rR   
