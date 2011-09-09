# A debugging web proxy
Yes, not for compression, not for caching, but for **debugging**.

## It can work transparently.
you can set an absolute address.  That is to say that all traffic going to port, say 85, should be forwarded 
to port 80, after it's dumped to the screen of course.

## Output
The proxy itself outputs in JSON messages: One per newline. Newlines in payloads are escaped. Nice, glossy UIs (or ugly obscene ones) are up to you.

## Format
An example of some common output would look like this:

    {"ts":1315359408.751000,"type":"payload","id":0,"
    text":"HTTP/1.1 200 OK\r\nDate: Wed, 07 Sep 2011 
    01:09:36 GMT\r\nStatus: 200 OK\r\nConnection: kee
    p-alive\r\nETag: fa147ca0-bb1b-012e-a5c1-704da221
    2453\r\nTransfer-Encoding: chunked\r\nContent-Typ
    e: text/plain\r\nSet-Cookie: sess=3aa3477df123eb2
    6c025b675bbfb8567; path=/; expires=Tue, 11-Oct-20
    11 18:29:36 GMT; HttpOnly\r\n\r\n"}

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

### { text: [ string ] }

For 

 * status : a number of flags ( see Flags )
 * payload : the data that is transiting ( see Data Notes )
 * error : a human readable description
 * close : the empty string

## Data Notes
Binary is encoded as \uXX where XX refers to the encoded character.

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

