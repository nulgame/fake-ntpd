# Fake ntpd

The simplest implementation of the NTP version 4 protocol.

The service runs on UDP port 123, just like a regular ntpd service. _fntpd_ runs on all network interfaces.
It speeds up execution time by 20 seconds/sec or 60 seconds/sec for testing of your business processes.

## Building

You can use any C compiler for building fake ntpd, here example with gcc:

```shell
gcc -o fntpd fntpd-service.c
```

## Running

On the server where you plan to run fntpd, you must stop the original ntpd (if it is already running).
Your server time is not affected by fntpd. Using nohuo command and ampersand you can push fntpd into background. 

```shell
# ./fntpd
```

To run as a daemon use following command:
```shell
# nohup ./fntpd &
```

You can use an additional switches:
- -d to get debuging information (on the std output), and/or
- -x for 3 times speeding (1 min/sec)

## Using

You can use following ntp clients to connect to the _fntpd_ service.

- on Mac: sntp YOUR-FNTPD-IP
- on Windows: net time ....
- on Linux/BSD: ntpdate YOUR-FNTPD-IP

Example of usage on Linux machine:
```shell
# ntpdate -q YOUR-FNTPD-IP  # to see new time
# ntpdate YOUR-FNTPD-IP     # to sync new time
```
