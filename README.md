# A debugging web proxy
Yes, not for compression, not for caching, but for **debugging**.

## It can work transparently.
you can set an absolute address.  That is to say that all traffic going to port, say 85, should be forwarded 
to port 80, after it's dumped to the screen of course.

## Output
The proxy itself outputs in JSON messages: One per newline. Newlines in payloads are 
escaped. Nice, glossy UIs (or ugly obscene ones) are up to you. A really basic 
filtering and extraction engine written in ruby is included in the examples directory.  
In classic ruby mentality, it is left undocumented.

## Format
An example of some common output would look like this:

    {"ts":1315359408.751000,"type":"payload","id":0,"
    ip":"127.0.0.1","port":33872,"text":"HTTP/1.1 200
    OK\r\nDate: Wed, 07 Sep 2011 01:09:36 GMT\r\nStat
    us: 200 OK\r\nConnection: keep-alive\r\nETag:fa14
    7ca0-bb1b-012e-a5c1-704da2212453\r\nTransfer-Enco
    ding: chunked\r\nContent-Type: text/plain\r\nSet-
    Cookie: sess=3aa3477df123eb26c025b675bbfb8567; pa
    th=/; expires=Tue, 11-Oct-2011 18:29:36 GMT; Http
    Only\r\n\r\n"}

The fields are explained below

### { ts: [ number ] }
The Epoch time with microsecond precision as returned by gettimeofday(2).

### { type: [ "info", "status", "payload", "error", "close" ] }
One of the following:

 * info : Overhead information about the proxy itself
 * status : The current state of the connected socket
 * payload : Data being sent over the wire
 * error : An internal error that prevented a connection from happening.
 * close : The connected socket is closed

### { id: [ number ] }
The proxy is of course, multi-plexed, serving multiple connections simultaneously.  These connections are indexed in order to keep track of them, this number is their index. 
The master connection is "-1" and only shows up when bringing things up or down.

### { ip: [ string ] }
The ip address of the client machine that is connected

### { port: [ number] }
The outgoing port that is being used by the client.

### { text: [ string ] }

For 

 * status : a number of flags ( see Flags )
 * payload : the data that is transiting ( see Data Notes )
 * error : a human readable description
 * close : the empty string

## Data Notes
Binary is encoded as \uXXYY where XX is the MSB (left-most) byte and YY is the LSB (right-most) byte.

## Flags
The flags are unnecessarily compact and overly confusing; but if you really want to know what they mean, pay close attention.

 1. All flags are 4 characters wide.
 2. The flags report on the state of transit from the server and the client
 3. Each character specifies a style of transit.

The semantic meanings are as follows:

 * Byte 0, value 'r', indicates that data has been read from the client (usually person with a web browser)
 * Byte 1, value 'R', indicates that data has been read from the server (usually where Apache or IIS is living)
 * Byte 2, value 'w', indicates that data is being written to the client (such as a 200 OK response as read from the server)
 * Byte 3, value 'W', indicates that data is being written to the server (such as a GET / request from the client)

