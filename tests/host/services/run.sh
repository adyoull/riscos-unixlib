#!/bin/sh -e
# Host test for the services database: netlib/ul_serv.c (the parser),
# netlib/ul_readline.c and netlib/getserv_r.c, against a test services file.
cd "$(dirname "$0")"
mkdir -p out
gcc -O1 -g -Wall -Ifake ../../../libunixlib/netlib/ul_serv.c \
  ../../../libunixlib/netlib/ul_readline.c ../../../libunixlib/netlib/getserv_r.c \
  test_services.c -o out/test_services
cd out
printf '# test services\nhttp\t80/tcp\twww www-http\n   \nssh 22/tcp\nbroken\nntp 123/udp\n' > 'InetDBase:Services'
./test_services
