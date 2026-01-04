# Fake ntpd

The simplest implementation of the NTP version 4 protocol.

The service runs on UDP port 123, just like a regular ntpd service. Runs on all network interfaces.
It speeds up execution time by 20 seconds/sec or 60 seconds/sec for testing of your business processes.

## Building

You can use any C compiler for building fake ntpd, here example with gcc:

```shell
gcc -o fntpd fntpd-service.c
```

## Running

On the server where you plan to run fntpd, you must stop the original ntpd (if it is already running).
Your server time is not affected by fntpd.

```shell
# ./fntpd
```

Additional switches:
- -d for debuging information (on the std output), and
- -x for 3 times speeding (1 min / 1 sec)

## Using

You can use following ntp clients to connect to the _fntpd_.

- on Mac: sntp YOUR-FNTPD-IP
- on Windows: net time ....
- on Linux/BSD: ntpdate YOUR-FNTPD-IP

Example of usage on Linux machine:
```shell
# ntpdate -q YOUR-IP-FNTPD  # to see new time
# ntpdate YOUR-IP-FNTPD     # to sync new time
```
